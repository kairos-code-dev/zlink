[한국어](multipart-atomicity.ko.md)

# Multipart 원자성 내부 설계

## 목적

이 문서는 `zlink`가 multipart message의 `send`/`recv` 원자성을 어떤 방식으로
확보하는지, 그리고 그 방식이 `libzmq`와 어디서 같고 어디서 다른지를 내부 구현
기준으로 정리한다.

여기서 말하는 원자성은 다음 의미를 가진다.

- sender 관점:
  multipart의 일부 frame만 queue에 남고 나머지가 사라지는 상태를 외부 peer가
  관찰하지 않게 한다.
- receiver 관점:
  하나의 multipart를 읽기 시작한 뒤 중간 frame 경계에서 다른 메시지와 섞이거나,
  반쪽짜리 message를 public API에 노출하지 않는다.

이 문서는 **raw socket public API**를 기준으로 설명한다.

- `zlink_send()`
- `zlink_send_rid()`
- `zlink_recv()`
- `zlink_subscribe()`

`SPOT` 같은 service handle은 일부 다른 semantics를 가지므로 별도 섹션에서
설명한다.

---

## 요약

현재 `zlink`의 multipart 원자성은 세 계층이 맞물려서 성립한다.

1. send 계층
   multipart를 frame sequence로 보내되, 중간 실패 시 socket rollback을 호출한다.
2. pipe / socket 계층
   각 frame의 `more` 경계와 rollback으로 미완성 multipart를 queue에서 제거한다.
3. public recv 계층
   첫 frame을 받은 뒤에는 follow-up frame을 내부 assembly로 끝까지 모은 후
   caller에 payload를 노출한다.

핵심은 다음 두 문장으로 요약할 수 있다.

- `send`는 multipart를 **원본 frame sequence 그대로** socket에 흘려 보낸다.
- `recv`는 multipart를 **중간에 끊어서 caller에 노출하지 않고** 내부에서
  assembly한 뒤 반환한다.

---

## 용어

- frame:
  `zlink_msg_t` 하나. `libzmq`에서는 `zmq_msg_t`.
- multipart message:
  `more` flag로 이어지는 frame sequence. 마지막 frame에는 `more`가 없다.
- payload part:
  공개 수신/콜백에서 사용자에게 노출되는 프레임.
- internal prefix:
  routing id, topic, 기타 protocol framing처럼 public payload cap 계산에는
  포함되지 않지만 내부 전송/조립에는 포함되는 frame.
- ypipe:
  `libzmq`/`zlink` 내부의 락-프리 SPSC(단일 생산자-단일 소비자) 큐.
  pipe 계층의 실제 데이터 전달을 담당한다.
- incomplete flag:
  `ypipe_t::write(value, incomplete)`의 두 번째 인자. `incomplete == true`이면
  해당 item은 flush(소비자에게 노출) 대상에서 제외된다. multipart의 중간 frame은
  항상 `incomplete == true`로 기록된다.

---

## Send 원자성

### 1. public send entry

raw socket public multipart send entry는 다음 경로를 탄다.

- `zlink_send()` / `zlink_send_rid()`
- [socket_message_send_api.cpp](../../src/api/socket/socket_message_send_api.cpp)

single-part는 별도 fast path를 타고,
multipart는 `logical_multipart_send*()`로 내려간다.

핵심 구현은:

- [multipart_send_txn.cpp](../../src/runtime/core/multipart_send_txn.cpp)

### 2. 현재 방식

현재 `zlink`는 multipart send에서 **caller 원본 frame을 직접 사용**한다.

- `send_frames_once()`가 `parts_[i]`를 그대로 `socket_->send()`에 넘긴다.
- 마지막 전 frame에는 `ZLINK_SNDMORE`를 붙인다.
- 중간 실패 시 `socket_->rollback()`을 호출한다.

관련 코드:

- [multipart_send_txn.cpp#L28](../../src/runtime/core/multipart_send_txn.cpp#L28)

즉 현재 구조는:

1. caller가 넘긴 `parts[]`
2. frame 순회
3. 각 frame을 socket에 직접 write
4. 중간 실패 시 rollback

이다.

### 3. rollback이 하는 일

socket rollback은 최종적으로 pipe rollback으로 이어진다.

- [socket_base_msg.cpp#L177](../../src/runtime/sockets/common/socket_base_msg.cpp#L177)
- [pipe.cpp#L367](../../src/runtime/core/pipe.cpp#L367)

pipe rollback은 outbound pipe에 기록된 **미완성 multipart**를 `unwrite()`로
되감는다.

핵심 성질:

- 완성되지 않은 multipart는 queue에 남지 않는다.
- receiver는 중간까지 enqueue된 frame만 따로 관찰하지 못한다.

### 4. socket type별 의미

`PAIR`, `DEALER`, `PUB`, `XPUB` 등은 각 socket의 write path가 `pipe`와
`dist/lb` 계층을 통해 multipart 경계를 유지한다.

예:

- load balancer:
  [lb.cpp](../../src/runtime/sockets/internal/lb.cpp)
- distributor:
  [dist.cpp](../../src/runtime/sockets/internal/dist.cpp)
- router:
  [router.cpp](../../src/runtime/sockets/router/router.cpp)

특히 `lb_t`는 multipart 도중 write 실패 시 rollback을 명시적으로 사용한다.

- [lb.cpp#L78](../../src/runtime/sockets/internal/lb.cpp#L78)

### 5. 현재 send 계약

public contract는 현재 구현과 맞추어 다음처럼 정리돼 있다.

- send attempt가 시작되면 ownership은 callee로 넘어간다.
- 성공/실패와 무관하게 input `zlink_msg_t`는 moved-from handle로 간주한다.
- caller는 실패 후 원본 `parts[]`를 재사용하면 안 된다.

관련 문서:

- [zlink.h#L799](../../include/zlink.h#L799)

이 계약은 direct-send + rollback 구조와 일치하며, caller가 moved-from
handle을 재사용하지 않는다는 단순한 규칙만 지키면 된다.

### 6. clone 없이 동작하는 이유

현재 구조는 retry/rollback을 위해 frame을 clone하지 않는다.

POSD 관점에서의 구성은 다음과 같다.

- 정상 경로:
  original frame direct send
- 실패 경로:
  socket/pipe rollback
- public contract:
  input moved-from

예외 상황을 위해 정상 경로를 clone하지 않으므로 small message hot path에
불필요한 clone 비용이 붙지 않고, single-part와 multipart 경로도 같은
공통 흐름을 공유한다.

---

## Recv 원자성

### 1. 내부 socket recv 계층

raw socket recv는 socket type별 `xrecv()` / `xrecv_routed()`를 통해 진행된다.

- [socket_base_msg.cpp](../../src/runtime/sockets/common/socket_base_msg.cpp)

multipart boundary를 실질적으로 유지하는 핵심은 `fq_t`와 `pipe_t`다.

- [fq.cpp](../../src/runtime/sockets/internal/fq.cpp)
- [pipe.cpp](../../src/runtime/core/pipe.cpp)

### 2. `fq_t`의 의미

`fq_t::recvpipe()`는 첫 part를 읽은 뒤 `_more`를 유지한다.

- [fq.cpp#L129](../../src/runtime/sockets/internal/fq.cpp#L129)

핵심 동작:

- 첫 part를 읽으면 `_more = true/false`
- `_more == true`인 동안은 같은 multipart의 후속 part를 기대
- 후속 part를 바로 읽지 못하면 partial을 버리고 `EAGAIN`을 반환(상위 helper가
  필요하면 `EPROTO`로 매핑)

관련 코드:

- [fq.cpp#L166](../../src/runtime/sockets/internal/fq.cpp#L166)

즉 socket 내부 semantics는 이미
"multipart를 시작했으면 중간 part 경계에서 정상적으로 끊기지 않는다"
는 모델이다.

### 3. public recv 계층

public raw recv는 socket 내부에서 받은 frame을 바로 caller에 하나씩 넘기지 않는다.
대신, 첫 payload frame 이후에는 내부 assembly를 수행한다.

핵심 구현:

- [socket_message_recv_api.cpp](../../src/api/socket/socket_message_recv_api.cpp)

동작:

1. 첫 frame 수신
2. `more` 확인
3. follow-up frame이 있으면 내부에서 계속 수집
4. 완성된 payload sequence를 TLS view로 export

즉 caller는:

- single-part면 part 1개
- multipart면 완성된 parts view

만 본다.

### 4. follow-up recv 의미론

follow-up frame은 일반 `recv timeout` 의미론이 아니라
**multipart assembly 의미론**으로 읽는다.

관련 코드:

- [recv_internal.cpp#L136](../../src/runtime/core/recv_internal.cpp#L136)

`recv_followup_msg_internal()`의 의미:

- 첫 part 이후 follow-up은 `ZLINK_DONTWAIT`로 조회
- `EAGAIN` / `EINTR`는 일반 timeout이 아니라 프로토콜 실패로 승격
- 호출자에게는 `EPROTO`로 반환

단, payload sequence export 등 일부 경로는 blocking follow-up
(`recv_followup_msg_socket_wait()`)으로 다음 part를 기다린다.

즉 raw socket public recv는 libzmq의 fq 모델과 같은 방향이다.

### 5. routed / subscribe shape

`ROUTER`, `STREAM`, `SUB/XSUB`는 internal framing을 일부 포함한다.

- routing id frame
- topic frame

public recv는 이 framing을 그대로 payload part로 노출하지 않는다.

예:

- `ROUTER`/`STREAM`:
  routing id를 `source_rid_out_`로 따로 전달
- `SUB`/`XSUB`:
  topic을 `topic_id_out_`로 따로 전달

즉 multipart assembly는 내부 frame 전체를 기준으로 하지만,
public contract는 payload part만 노출한다.

관련 코드:

- [socket_message_recv_api.cpp#L152](../../src/api/socket/socket_message_recv_api.cpp#L152)
- [socket_message_recv_api.cpp#L216](../../src/api/socket/socket_message_recv_api.cpp#L216)

### 6. TLS view 계약

public recv는 heap-owned `parts[]`를 반환하지 않는다.
현재는 thread-local view를 반환한다.

관련 문서:

- [zlink.h#L828](../../include/zlink.h#L828)
- [recv_tls_view.hpp](../../src/runtime/core/recv_tls_view.hpp)

caller 규칙:

- `parts_out_` 자체는 `free()` 금지
- 각 `zlink_msg_t`만 `zlink_msg_close()` 또는 `zlink_multipart_close()`로 정리
- 같은 스레드의 다음 recv-like call 전까지만 view 유효

이 설계는 recv hot path의 heap allocation을 제거하면서도
multipart를 aggregate shape로 설명하게 해 준다.

---

## Service / Spot 예외

Spot은 raw socket과 완전히 같은 atomicity surface가 아니다.

핵심 구현:

- [mesh_runtime.cpp](../../src/runtime/services/mesh/mesh_runtime.cpp)
- [mesh_dispatch_api.cpp](../../src/api/mesh/mesh_dispatch_api.cpp)

Spot direct 메시지와 Logical Multicast record는 raw part receive가 아니라
Spot claim의 receive batch(`zlink_mesh_claim_recv_batch()`)로 수신한다.
이 경로는 다음 성격을 띤다.

- ingress가 complete multipart를 mailbox admission 단위로 취급하므로
  batch에는 완성된 record만 나타난다
- 첫 message가 capacity에 들어가지 않으면 batch를 비운 채
  `BUFFER_TOO_SMALL`과 필요한 크기를 반환한다
- record·part view의 수명은 batch reset/destroy에 묶인다

즉 Spot은:

- raw socket fq invariant와 1:1로 동일한 surface가 아니라
- MeshNode dispatch runtime이 얹힌 higher-level claim consumer

이다.

따라서 raw socket atomicity 문장을 그대로 Spot에 복사하면 안 된다.
Spot 경로의 정식 계약은 dispatch spec
(`doc/spec/core/service/02-dispatch.ko.md`)이 소유하며, raw socket처럼
"첫 part 후 follow-up 즉시 EPROTO 승격"으로 설명되지 않는다.

---

## 콜백 경로와 원자성

직접 콜백/핸들러 경로도 결과적으로는 완성된 multipart를 콜백에
전달하는 방향으로 정리돼 있다.

- [socket_message_handler_api.cpp](../../src/api/socket/socket_message_handler_api.cpp)
- [socket_base_dispatch.cpp](../../src/runtime/sockets/common/socket_base_dispatch.cpp)

핵심 원칙:

- 콜백은 반쪽짜리 multipart를 part 단위로 흘려받지 않는다.
- 내부 dispatch가 multipart payload shape를 맞춰 콜백에 전달한다.

즉 public direct recv와 콜백 recv는 같은 payload shape 계약을 공유한다.

---

## libzmq 내부 구현 상세 분석

이 섹션은 `libzmq` 소스 코드를 계층별로 분석하여
multipart 원자성이 어떻게 성립하는지 정리한다.

분석 기준 소스:
`/home/hep7/project/kairos/libzmq/src/`

---

### 1. ypipe 계층: 락-프리 큐와 incomplete flag

multipart 원자성의 최하층은 `ypipe_t`(락-프리 SPSC 큐)이다.

핵심 파일:

- `libzmq/src/ypipe.hpp`
- `libzmq/src/ypipe_base.hpp`

`ypipe_t`는 세 개의 포인터로 동작한다.

```
_w : 마지막으로 flush된 위치 (writer thread 전용)
_f : 다음에 flush할 위치 (writer thread 전용)
_c : writer/reader 공유 atomic pointer (flush된 경계)
_r : 마지막으로 prefetch한 위치 (reader thread 전용)
```

multipart 원자성을 가능하게 하는 핵심은 `write()`의 `incomplete_` 파라미터다.

```cpp
// ypipe.hpp
void write (const T &value_, bool incomplete_)
{
    _queue.back () = value_;
    _queue.push ();
    if (!incomplete_)
        _f = &_queue.back ();   // flush 경계를 전진
}
```

동작 원리:

- `incomplete_ == true`: item을 queue에 넣지만 `_f`를 갱신하지 않는다.
  즉 이 item은 `flush()`를 호출해도 reader에 노출되지 않는다.
- `incomplete_ == false`: `_f`를 갱신하여 지금까지 쌓인 모든 item을
  다음 `flush()` 호출 시 reader에 노출 가능하게 한다.

multipart message의 중간 frame들은 `more` flag가 있으므로
`pipe_t::write()`에서 `incomplete_ = true`로 기록된다.
마지막 frame만 `incomplete_ = false`로 기록되어 전체 multipart가
한 번에 reader에 보인다.

`unwrite()`는 이 incomplete 경계를 이용한다.

```cpp
// ypipe.hpp
bool unwrite (T *value_)
{
    if (_f == &_queue.back ())
        return false;           // flush 경계 이후 아무것도 없음
    _queue.unpush ();
    *value_ = _queue.back ();
    return true;
}
```

`_f` 위치 이후에 쌓인 incomplete item들만 되감는다.
이미 flush 경계(`_f`)까지 도달한 complete message는 되감기지 않는다.
이것이 rollback이 "미완성 multipart만 정확히 제거"할 수 있는 이유다.

---

### 2. pipe 계층: write, flush, rollback

핵심 파일:

- `libzmq/src/pipe.hpp`
- `libzmq/src/pipe.cpp`

`pipe_t`는 `ypipe_t` 위에 HWM(high water mark) 제어, message 카운팅,
그리고 multipart 경계 관리를 추가한다.

#### 2.1. pipe_t::write()

```cpp
// pipe.cpp:222
bool zmq::pipe_t::write (const msg_t *msg_)
{
    if (unlikely (!check_write ()))
        return false;

    const bool more = (msg_->flags () & msg_t::more) != 0;
    const bool is_routing_id = msg_->is_routing_id ();
    _out_pipe->write (*msg_, more);    // more == true → incomplete
    if (!more && !is_routing_id)
        _msgs_written++;

    return true;
}
```

핵심 동작:

- `more == true`이면 `_out_pipe->write(*msg_, true)` → ypipe에 incomplete로 기록
- `more == false`이면 `_out_pipe->write(*msg_, false)` → flush 경계 전진
- message 카운팅은 마지막 frame에서만 수행 (`_msgs_written++`)
- HWM 검사(`check_write()`)도 message 단위이므로 multipart 중간에서
  HWM에 걸리면 write 실패를 반환

이 설계의 의미:

- reader 입장에서는 `flush()`가 호출되기 전까지 incomplete frame들이 보이지 않는다.
- 따라서 multipart의 일부 frame만 reader에 노출되는 상황이 원천적으로 차단된다.

#### 2.2. pipe_t::flush()

```cpp
// pipe.cpp:249
void zmq::pipe_t::flush ()
{
    if (_state == term_ack_sent)
        return;
    if (_out_pipe && !_out_pipe->flush ())
        send_activate_read (_peer);
}
```

`flush()`는 ypipe의 `_c` atomic pointer를 갱신하여
writer가 쓴 complete message를 reader가 볼 수 있게 만든다.

`ypipe_t::flush()`의 CAS 동작:

```cpp
// ypipe.hpp:76
bool flush ()
{
    if (_w == _f) return true;        // flush할 게 없음
    if (_c.cas (_w, _f) != _w) {      // CAS 실패 = reader가 sleeping
        _c.set (_f);
        _w = _f;
        return false;                 // reader 깨워야 함
    }
    _w = _f;
    return true;
}
```

- `_c.cas(_w, _f)`: 기존 flush 경계(`_w`)를 새 경계(`_f`)로 atomic하게 갱신
- CAS 실패 시 reader가 sleep 상태이므로 `send_activate_read`로 깨운다.

이 구조에서 multipart의 마지막 frame이 `write(msg, false)`로 기록된 뒤
socket type 구현이 `pipe->flush()`를 호출하면, 전체 multipart가
한 번에 reader에 노출된다.

#### 2.3. pipe_t::rollback()

```cpp
// pipe.cpp:236
void zmq::pipe_t::rollback () const
{
    msg_t msg;
    if (_out_pipe) {
        while (_out_pipe->unwrite (&msg)) {
            zmq_assert (msg.flags () & msg_t::more);
            const int rc = msg.close ();
            errno_assert (rc == 0);
        }
    }
}
```

동작:

- `unwrite()`로 flush 경계(`_f`) 이후의 incomplete item을 역순으로 꺼낸다.
- 꺼낸 item은 반드시 `more` flag가 있어야 한다 (assert로 검증).
- 각 item을 `close()`하여 메모리를 해제한다.

즉 rollback은 아직 flush되지 않은, `more` flag가 있는 중간 frame들만
정확히 제거한다. 이미 완성되어 flush된 이전 message에는 영향이 없다.

#### 2.4. pipe_t::terminate()에서의 rollback

pipe 종료 시에도 rollback을 통해 미완성 multipart를 정리한다.

```cpp
// pipe.cpp:434 (terminate 내부)
if (_out_pipe) {
    rollback ();                      // 미완성 outbound message 제거
    msg_t msg;
    msg.init_delimiter ();
    _out_pipe->write (msg, false);    // delimiter 전송
    flush ();
}
```

delimiter는 pipe 종료를 알리는 특수 message로,
rollback 후 보내므로 미완성 multipart 뒤에 delimiter가 섞이는 일이 없다.

---

### 3. fq 계층: fair queue recv와 multipart 경계

핵심 파일:

- `libzmq/src/fq.hpp`
- `libzmq/src/fq.cpp`

`fq_t`는 복수의 inbound pipe에서 round-robin으로 message를 읽는다.
multipart 원자성은 `_more` 상태 변수로 관리된다.

#### 3.1. fq_t 상태 변수

```cpp
// fq.hpp
pipes_t::size_type _active;    // 활성 pipe 수
pipes_t::size_type _current;   // 현재 읽고 있는 pipe index
bool _more;                    // multipart 읽기 중 여부
```

#### 3.2. fq_t::recvpipe() 상세

```cpp
// fq.cpp:52
int zmq::fq_t::recvpipe (msg_t *msg_, pipe_t **pipe_)
{
    int rc = msg_->close ();
    errno_assert (rc == 0);

    while (_active > 0) {
        const bool fetched = _pipes[_current]->read (msg_);

        if (fetched) {
            if (pipe_)
                *pipe_ = _pipes[_current];
            _more = (msg_->flags () & msg_t::more) != 0;
            if (!_more) {
                _current = (_current + 1) % _active;  // 다음 pipe로 이동
            }
            return 0;
        }

        //  Check the atomicity of the message.
        //  If we've already received the first part of the message
        //  we should get the remaining parts without blocking.
        zmq_assert (!_more);                            // ← 핵심 assert

        _active--;
        _pipes.swap (_current, _active);
        if (_current == _active)
            _current = 0;
    }

    rc = msg_->init ();
    errno_assert (rc == 0);
    errno = EAGAIN;
    return -1;
}
```

핵심 동작 분석:

1. **round-robin 고정**: `_more == true`인 동안은 `_current`를 변경하지 않는다.
   같은 pipe에서 multipart의 나머지 frame을 계속 읽는다.

2. **원자성 단언(assert)**: read가 실패하면 `zmq_assert(!_more)`를 실행한다.
   `_more == true` 상태에서 read 실패는 "multipart 중간에 데이터가 없다"는
   의미이므로 **프로세스를 abort**한다. 이것이 libzmq의 multipart recv
   원자성 보장 방식이다 — 위반 시 즉각 종료.

3. **pipe 비활성화**: 일반적인 read 실패(multipart가 아닌 경우)에서는
   해당 pipe를 active 목록에서 제거하고 다음 pipe를 시도한다.

4. **round-robin 전진**: multipart의 마지막 frame(`_more == false`)을 읽으면
   `_current`를 다음 pipe로 전진시켜 공정한 분배를 유지한다.

#### 3.3. fq_t::has_in()

```cpp
// fq.cpp:96
bool zmq::fq_t::has_in ()
{
    if (_more)
        return true;        // multipart 읽기 중이면 항상 데이터 있음

    while (_active > 0) {
        if (_pipes[_current]->check_read ())
            return true;
        // pipe 비활성화...
    }
    return false;
}
```

`_more == true`이면 무조건 `true`를 반환한다.
multipart 도중에는 poll/select에서 항상 readable로 보고된다.

#### 3.4. zlink의 fq_t 차이

`zlink`의 `fq_t::recvpipe()`는 같은 구조를 따르되 한 가지 차이가 있다.

`libzmq`는 `_more == true` 상태에서 read 실패 시 `zmq_assert(!_more)`로
**프로세스를 abort**한다. 이는 sender가 atomicity를 지켰다면
절대 발생하지 않아야 하는 상황이라는 가정이다.

`zlink`는 같은 상황에서 partial을 버리고 **EAGAIN(일시적 miss)을 반환**한다.

```cpp
// zlink fq.cpp — recvpipe()
if (_more) {
    _more = false;
    _active--;
    _pipes.swap (_current, _active);
    if (_current == _active)
        _current = 0;
    rc = msg_->init ();
    errno_assert (rc == 0);
    errno = EAGAIN;
    return -1;
}
```

이 차이는 의도한 것이다. pipe가 multipart 전송 도중 disconnect될 수 있는
상황(네트워크 끊김 등)에서 프로세스를 죽이는 대신 partial을 버리고 일시적
miss(EAGAIN)로 보고한다.

---

### 4. lb 계층: load balancer send와 multipart 원자성

핵심 파일:

- `libzmq/src/lb.hpp`
- `libzmq/src/lb.cpp`

`lb_t`는 outbound pipe에 round-robin으로 message를 보낸다.
`DEALER`, `PUSH`, `REQ` 등이 사용한다.

#### 4.1. lb_t 상태 변수

```cpp
// lb.hpp
pipes_t::size_type _active;    // 활성 pipe 수
pipes_t::size_type _current;   // 현재 write 중인 pipe index
bool _more;                    // multipart 쓰기 중 여부
bool _dropping;                // 실패한 multipart의 나머지를 drop 중
```

`_dropping`은 `fq_t`에 없는 상태다. send 쪽의 특수한 실패 처리를 위해 존재한다.

#### 4.2. lb_t::sendpipe() 상세

```cpp
// lb.cpp:56
int zmq::lb_t::sendpipe (msg_t *msg_, pipe_t **pipe_)
{
    //  Drop 모드: 실패한 multipart의 나머지 frame을 조용히 소비
    if (_dropping) {
        _more = (msg_->flags () & msg_t::more) != 0;
        _dropping = _more;          // 마지막 frame이면 drop 모드 해제

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;                   // caller에게는 성공으로 보고
    }

    while (_active > 0) {
        if (_pipes[_current]->write (msg_)) {
            if (pipe_)
                *pipe_ = _pipes[_current];
            break;
        }

        //  Multipart 도중 write 실패 → rollback + drop 모드 진입
        if (_more) {
            _pipes[_current]->rollback ();
            _dropping = (msg_->flags () & msg_t::more) != 0;
            _more = false;
            errno = EAGAIN;
            return -2;              // 특수 반환 코드
        }

        // pipe 비활성화 후 다음 pipe 시도
        _active--;
        if (_current < _active)
            _pipes.swap (_current, _active);
        else
            _current = 0;
    }

    if (_active == 0) {
        errno = EAGAIN;
        return -1;
    }

    //  마지막 frame이면 flush하고 다음 pipe로 이동
    _more = (msg_->flags () & msg_t::more) != 0;
    if (!_more) {
        _pipes[_current]->flush ();
        if (++_current >= _active)
            _current = 0;
    }

    const int rc = msg_->init ();
    errno_assert (rc == 0);
    return 0;
}
```

핵심 동작 분석:

1. **round-robin 고정**: `_more == true`이면 같은 pipe에 계속 write.
   마지막 frame에서만 `_current`를 전진시킨다.

2. **flush 타이밍**: 마지막 frame(`_more == false`)에서만 `flush()`를 호출한다.
   이것이 ypipe의 incomplete flag와 맞물려 reader가 완성된 multipart만
   볼 수 있게 한다.

3. **multipart 도중 실패**: `_more == true` 상태에서 `write()` 실패 시:
   - `rollback()`으로 이미 쓴 incomplete frame을 제거한다.
   - `_dropping = true`로 전환하여 caller가 보내는 나머지 frame을
     조용히 소비한다.
   - `-2`를 반환하여 `socket_base_t::send()`에 특수 상황을 알린다.

4. **drop 모드의 의미**: `_dropping == true` 동안 caller가 보내는 frame은
   `close()` + `init()`으로 즉시 폐기되지만 **caller에게는 성공으로 보고**된다.
   이는 libzmq의 하위호환성 보장 방식이다 — caller가 multipart의 나머지
   frame을 보내야 `_more` 상태를 빠져나갈 수 있기 때문이다.

5. **-2 반환 코드와 socket_base_t::send()의 처리**:

```cpp
// socket_base.cpp:1243
if (unlikely (rc == -2)) {
    if (!((flags_ & ZMQ_DONTWAIT) || options.sndtimeo == 0)) {
        rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;           // blocking 모드: 조용히 성공으로 처리
    }
}
```

blocking 모드에서는 multipart 도중 pipe 사망을 caller에 알리지 않는다.
이는 backward compatibility를 위한 타협이다.

#### 4.3. lb_t::pipe_terminated()에서의 drop 진입

```cpp
// lb.cpp:24
void zmq::lb_t::pipe_terminated (pipe_t *pipe_)
{
    //  If we are in the middle of multipart message and current pipe
    //  have disconnected, we have to drop the remainder of the message.
    if (index == _current && _more)
        _dropping = true;
    // ...
}
```

multipart 전송 중 현재 pipe가 terminate되면 즉시 `_dropping = true`로 전환한다.

---

### 5. dist 계층: distributor send와 multipart 원자성

핵심 파일:

- `libzmq/src/dist.hpp`
- `libzmq/src/dist.cpp`

`dist_t`는 `PUB`, `XPUB` 등이 사용하는 fan-out distributor다.
하나의 message를 matching하는 모든 pipe에 동시에 보낸다.

#### 5.1. dist_t 상태 변수

```cpp
// dist.hpp
pipes_t::size_type _matching;   // 현재 매칭된 pipe 수
pipes_t::size_type _active;     // 활성 pipe 수
pipes_t::size_type _eligible;   // eligible pipe 수 (active + 새로 attach된)
bool _more;                     // multipart 쓰기 중 여부
```

세 단계 pipe 분류:

```
[0, _matching)   : 이번 send에 참여하는 pipe
[_matching, _active) : 활성이지만 이번 send에 미참여
[_active, _eligible) : eligible하지만 현재 비활성 (HWM 등)
[_eligible, size)    : 비활성
```

#### 5.2. dist_t::send_to_matching() 상세

```cpp
// dist.cpp:127
int zmq::dist_t::send_to_matching (msg_t *msg_)
{
    const bool msg_more = (msg_->flags () & msg_t::more) != 0;

    distribute (msg_);

    //  Multipart 완료 시 eligible pipe를 다시 활성화
    if (!msg_more)
        _active = _eligible;

    _more = msg_more;
    return 0;
}
```

핵심: multipart 도중에는 `_active`를 변경하지 않는다.
새로 attach된 pipe는 `_eligible`까지만 들어가고 `_active`에는 포함되지 않는다.
이렇게 하면 multipart 도중 새로 연결된 pipe가 중간 frame부터 받는 일이 없다.

#### 5.3. dist_t::distribute() 상세

```cpp
// dist.cpp:144
void zmq::dist_t::distribute (msg_t *msg_)
{
    if (_matching == 0) {
        int rc = msg_->close ();      // matching pipe 없으면 drop
        // ...
        return;
    }

    if (msg_->is_vsm ()) {
        // VSM(very small message): 각 pipe에 직접 복사
        for (pipes_t::size_type i = 0; i < _matching;) {
            if (!write (_pipes[i], msg_)) { } else { ++i; }
        }
        int rc = msg_->init ();
        return;
    }

    //  일반 message: reference count 사용
    msg_->add_refs (static_cast<int> (_matching) - 1);

    int failed = 0;
    for (pipes_t::size_type i = 0; i < _matching;) {
        if (!write (_pipes[i], msg_)) {
            ++failed;
        } else {
            ++i;
        }
    }
    if (unlikely (failed))
        msg_->rm_refs (failed);       // 실패한 pipe 수만큼 ref 제거

    const int rc = msg_->init ();
}
```

write 실패 시 해당 pipe를 matching/active/eligible에서 제거하지만
다른 pipe로의 전송은 계속한다. 즉 fan-out에서 일부 pipe 실패는
나머지 pipe의 원자성에 영향을 주지 않는다.

#### 5.4. dist_t::write()와 flush 타이밍

```cpp
// dist.cpp:196
bool zmq::dist_t::write (pipe_t *pipe_, msg_t *msg_)
{
    if (!pipe_->write (msg_)) {
        // pipe를 matching/active/eligible에서 순서대로 제거
        _pipes.swap (_pipes.index (pipe_), _matching - 1);
        _matching--;
        _pipes.swap (_pipes.index (pipe_), _active - 1);
        _active--;
        _pipes.swap (_active, _eligible - 1);
        _eligible--;
        return false;
    }
    if (!(msg_->flags () & msg_t::more))
        pipe_->flush ();              // 마지막 frame에서만 flush
    return true;
}
```

lb_t와 동일하게 마지막 frame에서만 `flush()`를 호출한다.

#### 5.5. dist_t::attach()에서의 multipart 보호

```cpp
// dist.cpp:20
void zmq::dist_t::attach (pipe_t *pipe_)
{
    if (_more) {
        // multipart 전송 중: eligible에만 추가, active에는 넣지 않음
        _pipes.push_back (pipe_);
        _pipes.swap (_eligible, _pipes.size () - 1);
        _eligible++;
    } else {
        // 평상시: active에 직접 추가
        _pipes.push_back (pipe_);
        _pipes.swap (_active, _pipes.size () - 1);
        _active++;
        _eligible++;
    }
}
```

이 코드가 multipart 도중 새로 연결된 subscriber가
중간 frame부터 받는 것을 방지한다.

---

### 6. socket type별 multipart 원자성 패턴

#### 6.1. PAIR

```cpp
// pair.cpp:57
int zmq::pair_t::xsend (msg_t *msg_)
{
    if (!_pipe || !_pipe->write (msg_)) {
        errno = EAGAIN;
        return -1;
    }
    if (!(msg_->flags () & msg_t::more))
        _pipe->flush ();
    const int rc = msg_->init ();
    return 0;
}
```

가장 단순한 패턴. pipe가 하나뿐이므로 lb/dist 없이 직접 write.
마지막 frame에서만 flush. rollback은 pipe_t에 위임.

#### 6.2. DEALER

```cpp
// dealer.cpp:74-82
int zmq::dealer_t::xsend (msg_t *msg_)
{
    return sendpipe (msg_, NULL);      // lb_t::sendpipe()로 위임
}
int zmq::dealer_t::xrecv (msg_t *msg_)
{
    return recvpipe (msg_, NULL);      // fq_t::recvpipe()로 위임
}
```

DEALER는 lb_t(send)와 fq_t(recv)의 조합으로 동작한다.
multipart 원자성은 lb_t와 fq_t가 각각 보장한다.

#### 6.3. ROUTER

ROUTER는 multipart 처리가 가장 복잡하다.

**send 경로:**

```cpp
// router.cpp:161
int zmq::router_t::xsend (msg_t *msg_)
{
    //  첫 번째 part는 routing id
    if (!_more_out) {
        zmq_assert (!_current_out);

        if (msg_->flags () & msg_t::more) {
            _more_out = true;

            //  routing id로 pipe를 lookup
            out_pipe_t *out_pipe = lookup_out_pipe (...);

            if (out_pipe) {
                _current_out = out_pipe->pipe;
                if (!_current_out->check_write ()) {
                    // pipe 사용 불가 → mandatory면 에러, 아니면 무시
                    // ...
                }
            }
        }
        // routing id frame은 소비하고 반환
        int rc = msg_->close ();
        rc = msg_->init ();
        return 0;
    }

    //  이후 part들은 선택된 pipe에 직접 write
    _more_out = (msg_->flags () & msg_t::more) != 0;

    if (_current_out) {
        const bool ok = _current_out->write (msg_);
        if (unlikely (!ok)) {
            const int rc = msg_->close ();
            _current_out->rollback ();      // ← 실패 시 rollback
            _current_out = NULL;
        } else {
            if (!_more_out) {
                _current_out->flush ();     // ← 마지막 frame에서 flush
                _current_out = NULL;
            }
        }
    } else {
        const int rc = msg_->close ();      // pipe 없으면 drop
    }

    const int rc = msg_->init ();
    return 0;
}
```

ROUTER의 multipart 원자성 특징:

- routing id frame은 pipe lookup용으로만 사용되고 실제 wire에는 보내지 않는다.
- 이후 data frame들이 선택된 pipe에 sequential write된다.
- write 실패 시 `rollback()`으로 이미 쓴 frame을 제거한다.
- `_current_out`이 NULL이면(pipe 없음) 나머지 frame을 조용히 drop한다.

**recv 경로:**

```cpp
// router.cpp:263
int zmq::router_t::xrecv (msg_t *msg_)
{
    if (_prefetched) {
        if (!_routing_id_sent) {
            // prefetch된 routing id 반환
            const int rc = msg_->move (_prefetched_id);
            _routing_id_sent = true;
        } else {
            // prefetch된 data 반환
            const int rc = msg_->move (_prefetched_msg);
            _prefetched = false;
        }
        _more_in = (msg_->flags () & msg_t::more) != 0;
        if (!_more_in) {
            // multipart 끝 → current_in 정리
            _current_in = NULL;
        }
        return 0;
    }

    //  fq에서 다음 message를 읽음
    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (msg_, &pipe);

    // routing_id message 건너뛰기
    while (rc == 0 && msg_->is_routing_id ())
        rc = _fq.recvpipe (msg_, &pipe);

    if (_more_in) {
        // multipart 도중: 그냥 반환
        _more_in = (msg_->flags () & msg_t::more) != 0;
    } else {
        // multipart 시작: data를 prefetch에 저장하고 routing id를 먼저 반환
        rc = _prefetched_msg.move (*msg_);
        _prefetched = true;
        _current_in = pipe;

        // pipe의 routing id를 msg로 만들어 반환
        const blob_t &routing_id = pipe->get_routing_id ();
        rc = msg_->init_size (routing_id.size ());
        memcpy (msg_->data (), routing_id.data (), routing_id.size ());
        msg_->set_flags (msg_t::more);
        _routing_id_sent = true;
    }
    return 0;
}
```

ROUTER recv의 multipart 원자성 특징:

- prefetch 메커니즘: fq에서 읽은 첫 data frame을 `_prefetched_msg`에 저장하고,
  pipe의 routing id를 별도로 구성하여 먼저 반환한다.
- `_more_in`이 true인 동안은 같은 pipe에서 계속 읽는다 (fq가 보장).
- multipart 끝에서 `_current_in = NULL`로 현재 pipe를 해제한다.

**ROUTER rollback:**

```cpp
// router.cpp:334
int zmq::router_t::rollback ()
{
    if (_current_out) {
        _current_out->rollback ();
        _current_out = NULL;
        _more_out = false;
    }
    return 0;
}
```

#### 6.4. XPUB / PUB

XPUB는 `dist_t`를 통해 fan-out한다.

```cpp
// xpub.cpp:49
void zmq::xpub_t::xattach_pipe (pipe_t *pipe_, ...)
{
    _dist.attach (pipe_);
    // ...
}
```

multipart 원자성은 `dist_t`가 보장한다 (위 5절 참조).
PUB는 XPUB의 wrapper이므로 원자성도 동일하다.

---

### 7. socket_base_t::send() / recv()의 역할

핵심 파일:

- `libzmq/src/socket_base.cpp`

#### 7.1. send()

```cpp
// socket_base.cpp:1204
int zmq::socket_base_t::send (msg_t *msg_, int flags_)
{
    // context 유효성, message 유효성 검사
    // pending commands 처리

    //  more flag 설정
    msg_->reset_flags (msg_t::more);
    if (flags_ & ZMQ_SNDMORE)
        msg_->set_flags (msg_t::more);

    //  socket type의 xsend() 호출
    rc = xsend (msg_);
    if (rc == 0) return 0;

    //  -2 처리: multipart 도중 pipe 사망
    if (unlikely (rc == -2)) {
        if (!((flags_ & ZMQ_DONTWAIT) || options.sndtimeo == 0)) {
            // blocking 모드: 조용히 성공
            return 0;
        }
    }

    //  blocking 모드: 재시도 loop
    while (true) {
        process_commands (timeout, false);
        rc = xsend (msg_);
        if (rc == 0) break;
        // timeout 처리...
    }
    return 0;
}
```

핵심 역할:

- `ZMQ_SNDMORE` flag를 `msg_t::more`로 변환한다.
- socket type의 `xsend()`를 호출한다.
- blocking 모드에서 EAGAIN 시 재시도 loop를 돌린다.
- **-2 반환 코드 처리**: lb_t가 multipart 도중 pipe 사망을 보고하면
  blocking 모드에서는 조용히 성공으로 처리한다.

#### 7.2. recv()

```cpp
// socket_base.cpp:1292
int zmq::socket_base_t::recv (msg_t *msg_, int flags_)
{
    // context, message 유효성 검사

    //  command throttling
    if (++_ticks == inbound_poll_rate) {
        process_commands (0, false);
        _ticks = 0;
    }

    rc = xrecv (msg_);
    if (rc == 0) {
        extract_flags (msg_);     // _rcvmore 갱신
        return 0;
    }

    //  non-blocking / blocking 처리...
}
```

`extract_flags()`는 recv한 message의 `more` flag를 `_rcvmore` 멤버에 저장한다.
caller는 `zmq_getsockopt(ZMQ_RCVMORE)`로 이 값을 조회하여
multipart의 다음 frame이 있는지 확인한다.

```cpp
// socket_base.cpp:1754
void zmq::socket_base_t::extract_flags (const msg_t *msg_)
{
    if (unlikely (msg_->flags () & msg_t::routing_id))
        zmq_assert (options.recv_routing_id);
    _rcvmore = (msg_->flags () & msg_t::more) != 0;
}
```

이것이 libzmq의 frame-by-frame recv API의 핵심이다.
caller는 매번 recv 후 `RCVMORE`를 확인하고, true이면 다음 frame을 읽어야 한다.
이 protocol을 어기면(예: multipart 중간에 send를 호출) 정의되지 않은 동작이
발생한다.

---

## libzmq와의 비교

### 1. 공통점

`zlink`와 `libzmq`는 아래 핵심 철학이 같다.

- send는 multipart를 queue/pipe boundary 기준으로 atomic하게 다룬다.
- pipe는 `more`와 rollback으로 미완성 multipart를 제거한다.
- recv는 multipart를 시작한 뒤 part 사이에서 정상 timeout을 허용하지 않는다.

`libzmq` 기준 코드:

- `send`/`recv` 공통:
  `libzmq/src/socket_base.cpp`
- fair queue:
  `libzmq/src/fq.cpp`
- load balancer:
  `libzmq/src/lb.cpp`
- distributor:
  `libzmq/src/dist.cpp`
- pipe:
  `libzmq/src/pipe.cpp`
- ypipe:
  `libzmq/src/ypipe.hpp`

#### 1.1. ypipe incomplete flag가 동일한 역할을 한다

양쪽 모두 `pipe_t::write()`에서 `more` flag를 ypipe의 `incomplete` 파라미터로
전달한다. 중간 frame은 incomplete로, 마지막 frame은 complete로 기록되어
flush 전까지 reader가 미완성 multipart를 볼 수 없다.

#### 1.2. rollback 메커니즘이 구조적으로 동일하다

양쪽 모두 `pipe_t::rollback()`이 `ypipe_t::unwrite()`를 반복 호출하여
flush 경계 이후의 incomplete frame들을 제거한다.
`zmq_assert(msg.flags() & msg_t::more)` / `zlink_assert(msg.flags() & msg_t::more)`
로 되감긴 frame이 반드시 중간 frame임을 검증한다.

#### 1.3. flush 타이밍이 동일하다

lb_t, dist_t, pair_t 모두 마지막 frame(`more == false`) 전송 후에만
`pipe->flush()`를 호출한다. 이 패턴은 양쪽에서 동일하다.

#### 1.4. fq_t의 round-robin 고정이 동일하다

양쪽 모두 `_more == true`인 동안 `_current`를 변경하지 않아
같은 pipe에서 multipart의 나머지를 계속 읽는다.

### 2. 차이점

### 차이 1. public recv surface

`libzmq`:

- 기본 recv API는 frame-by-frame.
- caller가 `zmq_getsockopt(ZMQ_RCVMORE)`를 보고 multipart를 직접 조립.
- multipart assembly의 책임이 caller에 있다.

`zlink`:

- public recv는 aggregate multipart view.
- caller는 완성된 payload parts를 한 번에 받음.
- multipart assembly의 책임이 library 내부에 있다.

즉 atomicity의 내부 의미는 비슷하지만,
public API surface는 다르다.

### 차이 2. fq_t multipart 실패 처리

`libzmq`:

- `_more == true` 상태에서 read 실패 시 `zmq_assert(!_more)`로
  **프로세스를 abort**한다.
- sender가 원자성을 지켰다면 이 상황은 절대 발생하지 않는다는 가정.

`zlink`:

- 같은 상황에서 `_more = false`로 리셋하고 **EPROTO를 반환**한다.
- pipe disconnect 등으로 실제 발생 가능한 상황을 고려한 방어적 처리.

### 차이 3. lb_t의 drop 모드와 -2 반환

`libzmq`의 `lb_t`는 multipart 도중 pipe 사망 시 `_dropping = true`로
전환하고 `-2`를 반환한다. `socket_base_t::send()`는 이를 blocking 모드에서
조용히 성공으로 처리한다. 이는 backward compatibility를 위한 타협이다.

`zlink`의 multipart send(`multipart_send_txn.cpp`)는 중간 실패 시
`socket_->rollback()`을 호출하고 남은 frame들을 `consume_frames_from()`으로
정리한 뒤 에러를 반환한다. drop 모드 같은 중간 상태가 없다.

### 차이 4. caller ownership contract

`libzmq`:

- send/recv는 message-by-message API라 caller가 frame을 더 직접 관리.
- `zmq_send()`는 성공 시 message ownership을 가져가고,
  실패 시 caller가 계속 보유한다.
- multipart 중간에 send 실패하면 caller가 수동으로 복구해야 한다.

`zlink`:

- recv는 TLS multipart view.
- send는 attempt-begins-transfer 계약 (성공/실패 무관하게 ownership 이전).
- multipart send는 하나의 API call로 처리되므로 caller가 중간 상태를 관리할
  필요가 없다.

### 차이 5. dist_t의 multipart 보호와 새 pipe

`libzmq`의 `dist_t::attach()`는 `_more == true`일 때 새 pipe를
`_eligible`에만 추가하여 multipart 완료 후 `_active`로 승격한다.

`zlink`에서도 같은 패턴이 유지되며, 추가로 subscriber 측의
ready probe filtering이 있어 service 계층에서 한 겹 더 보호가 동작한다.

### 차이 6. service handles

`libzmq`는 raw socket library다.
`SPOT` 같은 service abstraction은 직접 제공하지 않는다.

`zlink`는 raw socket 위에 service runtime이 있어:

- ready probe filtering
- topic/routing framing 분리
- service-specific blocking semantics

이 추가된다.

따라서 `SPOT`은 raw socket atomicity 모델의 상위 추상화다.

---

## 현재 설계의 장점

### 1. hot path 단순화

- recv:
  heap-owned multipart array 제거
- send:
  clone/retry vector 제거

즉 normal path 비용이 줄었다.

### 2. public contract 단순화

- recv:
  "message만 close, array는 free 금지"
- send:
  "attempt begins transfer"

이 계약은 구현과 설명이 맞는다.

### 3. POSD 관점에서 깊은 모듈

caller가 알아야 하는 범위는 작게 유지된다.

- recv caller는 multipart assembly 내부를 알 필요가 없다.
- send caller는 clone/retry 구현을 알 필요가 없다.
- internal framing은 routing id/topic output으로 감춰진다.

shallow wrapper가 아니라 "복잡한 내부를 숨기는 깊은 module" 역할을 한다.

### 4. libzmq의 caller 책임 문제를 구조적으로 해소

`libzmq`의 frame-by-frame recv는 caller에게 다음 의무를 부과한다:

- `RCVMORE` flag를 매번 확인해야 한다.
- multipart 중간에 다른 작업을 하면 안 된다.
- recv loop를 완주하지 않으면 fq의 `_more` 상태가 오염된다.

`zlink`는 이 의무를 library 내부로 옮겨 caller의 실수 가능성을 없앴다.

---

## 현재 설계의 한계

### 1. raw socket recv와 SPOT recv는 동일 surface가 아니다

둘을 같은 atomicity 문장으로 설명하면 혼동이 생긴다.

### 2. libzmq와 public API shape는 다르다

내부 atomicity는 유사하지만,
`libzmq`처럼 caller가 `RCVMORE`를 직접 다루는 모델은 아니다.

### 3. TLS view는 lifetime contract를 요구한다

heap allocation 제거의 대가로:

- same-thread next recv 전까지만 유효
- `free(parts)` 금지

라는 규칙이 있다.

### 4. libzmq의 blocking send 호환 동작은 없다

`libzmq`는 multipart 도중 pipe 사망 시 blocking 모드에서 조용히 성공을
반환하는 backward compatibility 동작이 있다 (lb_t의 -2 / drop 모드).
`zlink`는 이 동작을 채택하지 않았으므로 caller에게 실패를 보고한다.
이는 더 정직한 API지만, libzmq에서 마이그레이션하는 코드는
이 차이를 인지해야 한다.

---

## 구현 검증 포인트

multipart atomicity가 유지된다고 보려면 아래가 계속 참이어야 한다.

1. send 중간 실패 시 peer가 반쪽짜리 multipart를 받지 않는다.
2. recv는 multipart 시작 후 일부 frame만 caller에 먼저 노출하지 않는다.
3. routed/topic prefix frame은 payload shape 밖으로 분리되지만,
   내부 assembly에는 포함된다.
4. callback/direct recv가 같은 payload shape contract를 유지한다.
5. bindings는 recv-owned parts 포인터를 `free()`하지 않는다.
6. ypipe의 incomplete flag와 flush 타이밍이 pipe_t::write()에서 올바르게
   전달된다 (more → incomplete=true, !more → incomplete=false).
7. rollback이 flush 경계 이후의 frame만 정확히 제거하며,
   이미 flush된 complete message에 영향을 주지 않는다.
8. dist_t가 multipart 도중 새로 attach된 pipe를 active로 승격하지 않아
   partial delivery가 발생하지 않는다.

---

## 관련 파일

- send
  - [core/src/runtime/core/multipart_send_txn.cpp](../../src/runtime/core/multipart_send_txn.cpp)
  - [core/src/api/socket/socket_message_send_api.cpp](../../src/api/socket/socket_message_send_api.cpp)
  - [core/src/runtime/sockets/internal/lb.cpp](../../src/runtime/sockets/internal/lb.cpp)
  - [core/src/runtime/core/pipe.cpp](../../src/runtime/core/pipe.cpp)
- recv
  - [core/src/runtime/core/recv_internal.cpp](../../src/runtime/core/recv_internal.cpp)
  - [core/src/api/socket/socket_message_recv_api.cpp](../../src/api/socket/socket_message_recv_api.cpp)
  - [core/src/runtime/sockets/internal/fq.cpp](../../src/runtime/sockets/internal/fq.cpp)
  - [core/src/runtime/core/recv_tls_view.hpp](../../src/runtime/core/recv_tls_view.hpp)
- service / spot
  - [core/src/runtime/services/mesh/mesh_runtime.cpp](../../src/runtime/services/mesh/mesh_runtime.cpp)
  - [core/src/api/mesh/mesh_dispatch_api.cpp](../../src/api/mesh/mesh_dispatch_api.cpp)
- public contract
  - [core/include/zlink.h](../../include/zlink.h)
- libzmq 참조
  - `libzmq/src/ypipe.hpp` — lock-free SPSC queue, incomplete flag
  - `libzmq/src/ypipe_base.hpp` — ypipe 인터페이스
  - `libzmq/src/pipe.hpp` / `pipe.cpp` — write, flush, rollback
  - `libzmq/src/fq.hpp` / `fq.cpp` — fair queue, _more assert
  - `libzmq/src/lb.hpp` / `lb.cpp` — load balancer, _dropping 상태
  - `libzmq/src/dist.hpp` / `dist.cpp` — distributor, _eligible 보호
  - `libzmq/src/socket_base.cpp` — send/recv entry, -2 처리
  - `libzmq/src/router.cpp` — ROUTER send/recv, prefetch, rollback
  - `libzmq/src/pair.cpp` — PAIR 단순 패턴
  - `libzmq/src/dealer.cpp` — DEALER lb+fq 조합
  - `libzmq/src/msg.hpp` — msg_t::more, msg_t::routing_id flag 정의

---

## 결론

현재 `zlink`의 raw socket multipart 원자성은 다음 식으로 이해하면 된다.

- send:
  direct frame send + socket/pipe rollback
- recv:
  socket fq invariant + public assembly-before-expose

이 모델은 내부 의미로는 `libzmq`와 매우 가깝다.
차이는 public surface다.

- `libzmq`는 frame-by-frame recv (caller가 RCVMORE 확인 및 assembly 책임)
- `zlink`는 aggregate multipart recv view (library가 assembly 책임)

양쪽 모두 원자성의 기반은 동일하다:

- ypipe의 incomplete flag가 중간 frame의 조기 노출을 차단한다.
- 마지막 frame에서만 flush하여 전체 multipart를 한 번에 reader에 노출한다.
- rollback은 flush 경계 이후의 incomplete frame만 정확히 제거한다.
- fq는 `_more` 상태로 multipart 도중 pipe를 고정한다.

즉 `zlink`는 `libzmq`의 atomicity 철학을 유지하면서,
public API는 더 높은 수준의 payload-shape contract로 감싼 구조로 보는 게
가장 정확하다.
