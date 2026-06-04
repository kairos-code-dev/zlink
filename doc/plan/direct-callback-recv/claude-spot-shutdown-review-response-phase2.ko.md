# Spot Shutdown Phase 2: `_term_acks` Assertion 검토 답변서

기준 커밋: `73dfc80f` (1차 수정: wait_drained socket tracking 보존)

---

## 1. 현재 상태 요약

### 1차 수정으로 해결된 것
- `wait_drained()` 타임아웃 시 `_closing_sockets` 추적 유실 → **수정 완료**
- abortive 경로에서 `tracked=3`이 찍혀 추적 보존을 확인
- 새 unit test 통과

### 2차로 드러난 문제
반복 stress 중 두 가지 증상이 나타났다.
1. split ctest 반복 중 `test_spot_service_introspection_explicit_handles` 60초 timeout
2. 단독 반복 중 `Assertion failed: _term_acks > 0 at own.cpp:153`

핵심은 이 테스트가 **TLS도, monitor도, TCP도 없는** 순수 inproc 테스트라는 점이다.
따라서 문제의 중심은 transport가 아니라 **pipe termination ack accounting**이다.

---

## 2. Root Cause 가설

### 가설 A: `term_endpoint()` + `close()` 시퀀스에서 pipe 상태에 따른 ack 불일치 (Primary)

`spot_pub_t::destroy_internal()`과 `spot_sub_t::destroy_internal()`은
`term_endpoint()`를 호출한 직후 곧바로 `close_socket()`을 호출한다.

```cpp
// spot_pub.cpp:556-567
if (socket) {
    if (_node)
        (void) socket->term_endpoint (_node->pub_ingress_endpoint ().c_str ());
    // ...
    if (_node && _node->_runtime)
        _node->_runtime->destroy_attachment (_attachment_id);  // → close_socket()
}
```

이 시퀀스에서 pipe 상태에 따라 ack 불일치가 생길 수 있다.

#### 핵심 메커니즘: `erase_pipes()` → `terminate(true)` → `_delay = true`

`term_endpoint()` → inproc 경로 → `_inprocs.erase_pipes()`:

```cpp
// socket_base.cpp:103-119
for (auto it = range.first; it != range.second; ++it) {
    it->second->send_disconnect_msg ();
    it->second->terminate (true);   // ← delay = true!
}
```

`terminate(true)`는 pipe를 `term_req_sent1`로 전환하면서
PIPE_TERM을 peer에 보낸다.

peer pipe (data plane측 ingress/fanout 소켓)가 PIPE_TERM을 받으면:

```cpp
// pipe.cpp:446-454
if (_state == active) {
    if (_delay)                     // ← peer의 _delay는 true (기본값)
        _state = waiting_for_delimiter;  // ★ ACK를 바로 보내지 않음!
    else {
        _state = term_ack_sent;
        send_pipe_term_ack (_peer);
    }
}
```

peer pipe의 `_delay`가 true이면 `waiting_for_delimiter` 상태로 진입하고
**PIPE_TERM_ACK를 즉시 보내지 않는다.**

#### 불일치 발생 시나리오

```
시간  attachment PUB          pipe_A(→)  pipe_B(←)         ingress SUB
 ─────────────────────────────────────────────────────────────────────
 T1   term_endpoint()
      erase_pipes:
        pipe_A.terminate(true)  → term_req_sent1
                                   PIPE_TERM →
                                               waiting_for_delimiter
                                               (ACK 보류)

 T2   close_socket()
      socket→stop()
      socket→close() → reaper

 T3                              ── reaper에서 process_term() ──
                                 _pipes = [pipe_A]
                                 pipe_A.terminate(false) → NO-OP (이미 term_req_sent1)
                                 register_term_acks(1)    _term_acks = 1
                                 own_t::process_term()    _terminating = true

 T4   ── data plane 아직 실행 중, ingress에서 recv 시도 ──
                                               데이터 플레인이 pipe_B에서
                                               delimiter를 읽음
                                               → process_delimiter()
                                               → term_ack_sent
                                               → PIPE_TERM_ACK → pipe_A

 T5                              pipe_A: process_pipe_term_ack()
                                   pipe_terminated() on PUB socket
                                   is_terminating() = true
                                   unregister_term_ack()  _term_acks = 0
                                   → pipe_A: send_pipe_term_ack to pipe_B
                                   → delete pipe_A

 T6                                            pipe_B: process_pipe_term_ack()
                                               pipe_terminated() on ingress
                                               is_terminating()가 ??
```

여기까지는 정상이다. 하지만 **타이밍이 다르면** 문제가 생긴다.

#### 타이밍 변형: pipe_B 완료가 먼저, ingress process_term이 나중에

```
 T1   term_endpoint() → pipe_A.terminate(true) → PIPE_TERM 전송

 T2   data plane thread에서 ingress가 recv() 중 process_commands() 호출
      pipe_B가 PIPE_TERM 수신
      _delay = true → waiting_for_delimiter
      이후 recv()가 delimiter를 읽음 → process_delimiter()
      → term_ack_sent, PIPE_TERM_ACK → pipe_A

 T3   close_socket() on PUB attachment → reaper
      reaper에서 process_term():
        pipe_A.terminate(false) → NO-OP
        register_term_acks(1)  _term_acks = 1

 T4   pipe_A가 PIPE_TERM_ACK 수신 → process_pipe_term_ack()
      pipe_terminated() → unregister_term_ack()  _term_acks = 0
      pipe_A가 PIPE_TERM_ACK를 pipe_B에 전송

 T5   pipe_B가 PIPE_TERM_ACK 수신
      process_pipe_term_ack()
      pipe_terminated() on ingress   ← ingress는 아직 terminating이 아님!
      is_terminating() = false → unregister_term_ack() 호출 안 함
      pipe_B가 ingress._pipes에서 제거됨

 T6   data plane thread 종료, ingress close → reaper
      process_term():
        _pipes.size() = 0 (pipe_B 이미 제거됨)
        register_term_acks(0)  _term_acks = 0
        own_t::process_term():  _terminating = true
        check_term_acks() → _term_acks = 0 → 즉시 destroy
```

이 시나리오에서는 ack가 올바르게 균형을 맞춘다 (0 registered, 0 unregistered).
**이 경로 자체는 정상이다.**

#### 문제가 되는 진짜 시나리오: 중간 상태에서의 새 pipe 연결

`ctx_t::terminate()`에서 pending inproc connection을 해소할 때:

```cpp
// ctx.cpp:219-227
pending_connections_t copy = _pending_connections;
for (...) {
    socket_base_t *s = create_socket (ZLINK_PAIR);
    s->bind (p->first.c_str ());
    s->close ();
}
```

spot 내부 소켓이 pending 상태였다면 (startup race),
이 해소 과정에서 **이미 reaper에서 terminating 중인 소켓에 새 pipe가 attach**된다.

```cpp
// socket_base.cpp:462-465
if (is_terminating ()) {
    register_term_acks (1);
    pipe_->terminate (false);
}
```

이 경로 자체는 올바르지만, `connect_inproc_sockets()`에서는
**application thread가 직접 `process_command(bind)`를 호출**한다.

```cpp
// ctx.cpp:996-999
command_t cmd;
cmd.type = command_t::bind;
cmd.args.bind.pipe = pending_connection_.bind_pipe;
bind_socket_->process_command (cmd);
```

이것이 **reaper thread와 동시에 같은 소켓의 `_pipes`/`_term_acks`에 접근**하면
thread safety 위반이 생길 수 있다.

### 가설 B: data plane 소켓 close와 attachment term_endpoint 간 pipe 상태 경합

`destroy_handles()`가 attachment pipe를 `term_endpoint()`로 종료한 뒤
**data plane thread가 아직 실행 중**인 상태에서 관련 소켓을 통해
pipe 명령이 처리된다.

data plane thread가 poll loop에서 `recv()` → `process_commands()`를 호출하면
pending PIPE_TERM/PIPE_TERM_ACK 명령을 **data plane thread의 context에서** 처리한다.

그런데 `stop_and_join()`은 `destroy_handles()` **이후**에 호출된다:

```
destroy_handles()         ← attachment pipe 종료, data plane 아직 실행 중
_runtime->stop_and_join() ← 여기서야 data plane thread 정지
```

이 창(window)에서 data plane측 pipe가 완료되면서
ingress/fanout 소켓의 `_pipes`에서 pipe가 제거되고,
이후 `process_term()`이 pipe를 못 세는 경우가 생길 수 있다.

### 가설 C: `terminate(true)` vs `terminate(false)` delay 불일치

`erase_pipes()`는 `terminate(true)` (delay=true)를 쓰는데
이후 `process_term()`이 같은 pipe에 `terminate(false)` (delay=false)를 호출한다.

```cpp
// pipe.cpp:521-529
void pipe_t::terminate (bool delay_)
{
    _delay = delay_;  // ← delay를 덮어씀!

    if (_state == term_req_sent1 || _state == term_req_sent2) {
        return;  // 일찍 반환하지만 _delay는 이미 변경됨
    }
    // ...
}
```

`_delay`가 true→false로 바뀌지만 함수는 즉시 return한다.
pipe 자체에는 영향이 없으나 **peer pipe의 응답 타이밍**은 peer의 `_delay`에 따라 달라진다.
이 비대칭이 드물게 ack 타이밍 불일치를 일으킬 수 있다.

---

## 3. 가장 가능성 높은 원인

**가설 B와 가설 A의 조합**이 가장 유력하다.

1. `destroy_handles()`가 attachment pipe를 `term_endpoint()`로 종료
2. data plane thread가 아직 실행 중이므로
   pipe_B(data plane측)가 data plane의 `recv()` → `process_commands()` 안에서
   PIPE_TERM을 처리하고 상태를 전환
3. pipe 완료 콜백(`pipe_terminated()`)이 data plane측 소켓에서 호출
4. 이 시점에서 data plane 소켓은 terminating이 아니므로 `unregister_term_ack()`를 호출하지 않음
5. pipe가 `_pipes`에서 제거됨
6. 나중에 data plane 소켓이 close → `process_term()` → `_pipes.size()`에 이미 제거된 pipe 미포함
7. 여기까지는 정상 (0 registered, 0 unregistered)

문제는 이 과정에서 **또 다른 pipe나 소유 객체의 ack가 어긋나는 경우**다.

- 같은 소켓에 여러 pipe가 있을 때
  일부는 `term_endpoint()`로 이미 완료되고 일부는 아직 active
- `process_term()`은 남은 pipe만 세지만
  완료된 pipe의 처리 순서에 따라 `pipe_terminated()` 콜백이
  `process_term()` 이후에 도착하면 **이미 0인 `_term_acks`를 또 감소**시킴

이 시나리오가 정확히 `_term_acks > 0` assertion과 일치한다.

---

## 4. 구조적 해결책

### 4.1 `term_endpoint()` 제거: 가장 직접적인 해법

`spot_pub_t::destroy_internal()`과 `spot_sub_t::destroy_internal()`에서
`term_endpoint()` 호출을 **제거**한다.

이유:
- `term_endpoint()`는 특정 endpoint의 pipe만 골라서 종료하는 API다
- 하지만 `destroy_internal()`에서는 소켓 자체를 곧바로 닫는다
- `close()` → reaper → `process_term()`이 **모든 pipe를 일괄 종료**하므로
  `term_endpoint()`의 선제 종료가 불필요하다
- 오히려 pipe 상태를 미리 바꿔 `process_term()`의 ack counting과 충돌한다

```cpp
// spot_pub.cpp: 수정 전
if (socket) {
    if (_node)
        (void) socket->term_endpoint (_node->pub_ingress_endpoint ().c_str ());
    unregister_spot_pub_socket (socket);
    if (_node && _node->_runtime)
        preserve_first_error (
          _node->_runtime->destroy_attachment (_attachment_id), &first_error);
}

// spot_pub.cpp: 수정 후
if (socket) {
    unregister_spot_pub_socket (socket);
    if (_node && _node->_runtime)
        preserve_first_error (
          _node->_runtime->destroy_attachment (_attachment_id), &first_error);
}
```

spot_sub.cpp도 똑같이 `term_endpoint()` 호출을 제거한다.

### 4.2 대안: `term_endpoint()` 유지 시 ack 보호

`term_endpoint()`를 유지해야 한다면
`pipe_terminated()`에서 **이미 제거된 pipe에 대한 중복 unregister를 막는다**.

```cpp
// socket_base.cpp:2027-2055
void zlink::socket_base_t::pipe_terminated (pipe_t *pipe_)
{
    xpipe_terminated (pipe_);
    _inprocs.erase_pipe (pipe_);

    // ★ pipe가 실제로 _pipes에 있었는지 확인
    const bool was_tracked = _pipes.erase (pipe_);

    // ... endpoint cleanup ...

    if (is_terminating () && was_tracked)
        unregister_term_ack ();
}
```

다만 현재 `_pipes.erase()`의 반환값이 void인지 bool인지 확인이 필요하다.
`array_t::erase()`가 실제로 무엇을 반환하는지에 따라 달라진다.

### 4.3 `destroy_handles()` 순서 변경

data plane thread를 먼저 정지한 뒤 handle을 destroy한다.

```cpp
// 현재 순서 (spot_node.cpp:1772-1777)
preserve_first_error (destroy_handles (), &first_error);
if (_runtime)
    preserve_first_error (_runtime->stop_and_join (), &first_error);

// 제안 순서
if (_runtime)
    preserve_first_error (_runtime->stop_and_join (), &first_error);
preserve_first_error (destroy_handles (), &first_error);
```

이렇게 하면 data plane thread가 이미 종료된 상태에서 handle을 destroy하므로
data plane측 소켓의 pipe 상태가 안정된 상태에서 작업한다.

단, `stop_and_join()` 내부에서 "terminate" 명령을 ctrl 채널로 보내는데
이 시점에 attachment pipe가 아직 살아 있으면
data plane의 `recv_and_forward()`가 에러를 내며 종료될 수 있다.
이 변경을 적용하려면 `stop_and_join()`의 동작도 함께 검토해야 한다.

---

## 5. 즉시 적용 가능한 최소 수정안

### 수정 1: `term_endpoint()` 호출 제거 (Recommended)

spot_pub.cpp:

```cpp
// spot_pub.cpp:556-567 수정
if (socket) {
    // term_endpoint() 제거 — close_socket()이 process_term()에서
    // 모든 pipe를 일괄 종료하므로 선제 종료 불필요
    unregister_spot_pub_socket (socket);
    if (_node && _node->_runtime)
        preserve_first_error (
          _node->_runtime->destroy_attachment (_attachment_id), &first_error);
    else {
        socket->stop ();
        socket->close ();
    }
}
```

spot_sub.cpp:

```cpp
// spot_sub.cpp:644-653 수정
if (socket) {
    // term_endpoint() 제거
    if (_node && _node->_runtime)
        preserve_first_error (
          _node->_runtime->destroy_attachment (_attachment_id), &first_error);
    else {
        socket->stop ();
        socket->close ();
    }
}
```

### 수정 2: `pipe_terminated()`에 방어적 guard (Safety net)

```cpp
// socket_base.cpp:2053-2054
if (is_terminating ())
    unregister_term_ack ();
```

이것을:

```cpp
if (is_terminating () && _term_acks > 0)
    unregister_term_ack ();
```

로 바꾸면 assertion crash는 막지만
**근본 원인(ack 불일치)을 가리므로 디버깅 목적으로만 권장한다.**

프로덕션에서는 assertion 대신 경고 로그가 더 안전하다:

```cpp
if (is_terminating ()) {
    if (_term_acks > 0)
        unregister_term_ack ();
    else
        fprintf (stderr, "[WARNING] pipe_terminated: _term_acks already 0, "
                 "skipping unregister for socket %p\n",
                 static_cast<void *> (this));
}
```

---

## 6. 장기 구조 개선안

### 6.1 pipe 종료 경로 단일화 원칙

현재 pipe를 종료하는 경로가 3개다:

| 경로 | 호출처 | `terminate(delay)` | ack 등록 |
|------|--------|-------------------|---------|
| `term_endpoint()` | 사용자 API | `terminate(true)` | 없음 |
| `process_term()` | reaper (socket close) | `terminate(false)` | `_pipes.size()` |
| `attach_pipe()` | terminating 중 새 pipe | `terminate(false)` | 1 |

`term_endpoint()`는 ack를 등록하지 않고 pipe를 종료한다.
이 pipe가 나중에 `pipe_terminated()` 콜백을 받을 때:
- socket이 아직 terminating이 아니면 → ack 감소 없음 (정상)
- socket이 terminating이면 → ack 감소 시도 (불일치 위험)

**원칙**: service 레벨 코드(`spot_pub`, `spot_sub`)에서는
`term_endpoint()`로 개별 pipe를 종료하지 말고
`close()` → `process_term()`의 일괄 종료에 맡긴다.

### 6.2 소멸 순서 직렬화

```
현재:   destroy_handles()  →  stop_and_join()  →  wait_drained()
              ↓                    ↓
         pipe 종료 시작       data plane 종료
         (data plane 실행 중)  (pipe 완료 경합)

제안:   stop_data_plane()  →  destroy_handles()  →  wait_drained()
              ↓                    ↓
         data plane 종료       pipe 종료
         (소켓 안정 상태)       (경합 없음)
```

이렇게 하면 data plane 소켓이 close된 뒤에 attachment pipe를 종료하므로
pipe 상태 경합이 원천적으로 사라진다.

### 6.3 `_term_acks` 디버그 계측

```cpp
#ifdef ZLINK_DEBUG_TERM_ACKS
void own_t::register_term_acks (int count_)
{
    fprintf (stderr, "[term-acks] register %d (was %d, now %d) at %p\n",
             count_, _term_acks, _term_acks + count_, this);
    _term_acks += count_;
}

void own_t::unregister_term_ack ()
{
    fprintf (stderr, "[term-acks] unregister 1 (was %d, now %d) at %p\n",
             _term_acks, _term_acks - 1, this);
    zlink_assert (_term_acks > 0);
    _term_acks--;
    check_term_acks ();
}
#endif
```

이 계측을 켜고 실패를 재현하면
어떤 소켓에서 register/unregister 불균형이 생기는지 정확히 추적할 수 있다.

---

## 7. 테스트 보강

### 7.1 반복 stress 테스트 자동화

```bash
#!/bin/bash
for i in $(seq 1 100); do
    echo "=== Run $i ==="
    env ZLINK_CTX_DEBUG=1 ZLINK_SPOT_SHUTDOWN_LOG=1 \
        core/build/bin/test_spot_service_introspection || {
        echo "FAILED at run $i"
        exit 1
    }
done
```

### 7.2 `_term_acks` 균형 검증 hook

소켓 소멸 시 `_term_acks`가 정확히 0인지 확인하는 debug assertion:

```cpp
void socket_base_t::finalize_destroy ()
{
#ifdef ZLINK_DEBUG
    if (_term_acks != 0)
        fprintf (stderr, "[BUG] socket %p destroyed with _term_acks=%d\n",
                 static_cast<void *> (this), _term_acks);
#endif
    // ... existing code ...
}
```

---

## 8. 권장 수정 우선순위

| 순위 | 수정 | 영향 | 위험도 |
|------|------|------|--------|
| 1 | `term_endpoint()` 호출 제거 (spot_pub, spot_sub) | pipe 상태 경합 원천 제거 | 낮음 |
| 2 | `_term_acks` 디버그 계측 추가 | 재현 시 정확한 추적 | 없음 |
| 3 | `pipe_terminated()`에 방어적 guard | crash 방지 (임시) | 낮음 |
| 4 | destroy 순서 변경 (stop_and_join 먼저) | data plane 경합 제거 | 중간 (부작용 검토 필요) |

수정 1이 가장 직접적이고 안전하다.
`term_endpoint()`는 "특정 endpoint에서 disconnect하되 소켓은 유지"하는
유스케이스를 위한 API인데, `destroy_internal()`에서는 소켓 자체를 파괴하므로
선제 `term_endpoint()`가 불필요할 뿐 아니라 해롭다.
