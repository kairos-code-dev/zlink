# Spot Shutdown Phase 3: 구조적 원인 분석

---

## 1. 왜 `term_endpoint()` 제거가 효과가 없었는가

`term_endpoint()` 경로는 ack 불일치의 원인이 아니었다.
실제 원인은 더 깊은 곳에 있다: **I/O 스레드와 reaper 스레드 간의 data race.**

---

## 2. 근본 원인: async mailbox의 스레드 경합

### 2.1 배경: direct callback의 async mailbox 구조

`spot_sub_t::set_direct_handler()` → `socket->sub_dispatch_start()` 호출 시:

```
start_async_mailbox_processing(io_thread):
  _async_mailbox_active = true
  mailbox->set_io_context(io_thread의 ASIO context,
                          async_mailbox_handler, ...)
```

이 순간부터 SUB attachment 소켓의 mailbox 명령은
**I/O 스레드**에서 `process_async_mailbox()` → `process_commands()`로 처리된다.

`process_commands()`는 pipe_term, pipe_term_ack 등
**모든 명령**을 처리하며, 그 결과로 `_pipes`, `_term_acks` 등
소켓 내부 상태를 변경한다.

### 2.2 race window: I/O 스레드 vs reaper 스레드

teardown 시퀀스:

```
Application thread          I/O thread                Reaper thread
─────────────────          ──────────                ─────────────
sub_dispatch_stop()
  _async_mailbox_active=false
  (I/O thread에 알림)
                           process_async_mailbox():
                             process_commands()
                               pipe_term_ack 수신
                               → pipe_terminated()
                               → _pipes.erase(P)    ←─── ★ _pipes 변경
                             _async_mailbox_active 확인
                             → false → I/O context 해제

close_socket()
  stop() → 명령 전송
  close() → reap 명령 전송
                                                     start_reaping():
                                                       set_io_context(reaper)
                                                       terminate()
                                                         process_term()
                                                           _pipes 순회  ←─── ★ 동시 접근!
                                                           register_term_acks(_pipes.size())
```

### 2.3 핵심 경합 지점

**`stop_async_mailbox_processing()`은 I/O 스레드 완료를 기다리지 않는다:**

```cpp
// socket_base.cpp:1770-1775
void socket_base_t::stop_async_mailbox_processing ()
{
    _async_mailbox_active.store (false, std::memory_order_release);
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->schedule_if_needed ();
    // ← 즉시 반환! I/O 스레드가 아직 process_commands() 중일 수 있음
}
```

**`start_reaping()`은 이전 I/O context의 완료를 확인하지 않는다:**

```cpp
// socket_base.cpp:1686-1701
void socket_base_t::start_reaping (poller_t *poller_)
{
    _poller = poller_;
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->set_io_context (...reaper context...);
    // ← 이전 I/O handler가 아직 실행 중이어도 새 context 설정
    mailbox->schedule_if_needed ();
    terminate ();     // ← process_term()이 _pipes를 순회
    check_destroy ();
}
```

### 2.4 결과: 두 스레드가 동시에 소켓 상태에 접근

| 자원 | I/O 스레드 (async handler) | Reaper 스레드 |
|------|---------------------------|--------------|
| `_pipes` | `pipe_terminated()` → erase | `process_term()` → iterate + terminate |
| `_term_acks` | (indirectly via `pipe_terminated`) | `register_term_acks()` |
| `_terminating` | `is_terminating()` 읽기 | `own_t::process_term()` → true 설정 |
| pipe 상태 | `process_pipe_term_ack()` | `pipe->terminate()` |

이것은 `_pipes`와 `_term_acks`가 **동기화 없이** 두 스레드에서
동시 접근되는 **data race**이며, 이로 인해:

- `_pipes.size()`가 부정확 → `register_term_acks()` 오류
- `pipe_terminated()`에서 이미 정리된 pipe 접근 → UB
- `_term_acks` 값이 0인데 `unregister_term_ack()` 호출 → assertion

### 2.5 왜 stress 테스트에서만 재현되는가

- 단일 실행: I/O 스레드가 `_async_mailbox_active = false`를 빠르게 확인하고 종료
- stress 반복: I/O 스레드가 많은 pending 명령을 처리 중이거나,
  스케줄링 지연으로 flag 확인이 늦어짐
- 결과: `close_socket()` → reaper가 시작될 때 I/O 스레드가 아직 활성

---

## 3. 왜 이것이 구조적 문제인가

하나를 고치면 다른 문제가 나오는 이유:

1. **1차 수정** (wait_drained tracking): lifecycle tracker 문제 해결
   → 더 빈번하게 소켓이 제대로 추적됨
   → reaper가 더 빠르게 소켓을 처리하기 시작
   → I/O 스레드와의 경합 window가 더 자주 열림

2. **2차 시도** (term_endpoint 제거): pipe 상태 경합 경로 하나 제거
   → 하지만 근본 원인인 I/O 스레드 vs reaper 동시 접근은 그대로

3. **구조적 결함**: 소켓의 내부 상태 (`_pipes`, `_term_acks`, `_terminating`)가
   **단일 스레드에서만 접근된다는 암묵적 가정**에 의존하는데,
   async mailbox가 이 가정을 위반한다.

---

## 4. 구조적 해결 방향

### 방향 A: I/O 스레드 완전 quiesce 후 close (권장)

`close_socket()`이 호출되기 **전에** I/O 스레드의 async handler가
완전히 종료되었음을 보장한다.

```
현재:
  sub_dispatch_stop()    ← _async_mailbox_active = false (비동기)
  close_socket()         ← 즉시 reaper로 전송
  ─→ I/O 스레드와 reaper가 동시 접근 가능

제안:
  sub_dispatch_stop()    ← _async_mailbox_active = false
  wait_async_quiesced()  ← I/O 스레드가 완전히 나올 때까지 대기 ★
  close_socket()         ← 이제 안전하게 reaper로 전송
```

구현 방법:

```cpp
// socket_base.hpp에 추가
std::atomic<bool> _async_processing_done;
condition_variable_t _async_done_cv;
mutex_t _async_done_mu;

// stop_async_mailbox_processing() 수정
void socket_base_t::stop_async_mailbox_processing ()
{
    _async_mailbox_active.store (false, std::memory_order_release);
    _async_processing_done.store (false, std::memory_order_release);
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->schedule_if_needed ();
}

// process_async_mailbox() 수정 - 종료 시 알림
void socket_base_t::process_async_mailbox ()
{
    do {
        process_commands (0, false);
        if (_destroyed) { check_destroy (); return; }
        if (_async_mailbox_active.load (std::memory_order_acquire))
            xdispatch_io ();
        if (!_async_mailbox_active.load (std::memory_order_acquire)) {
            mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
            mailbox->reschedule_if_needed ();
            mailbox->set_io_context (NULL, NULL, NULL, NULL);
            // ★ I/O 스레드 종료 알림
            _async_processing_done.store (true, std::memory_order_release);
            {
                scoped_lock_t lock (_async_done_mu);
                _async_done_cv.broadcast ();
            }
            return;
        }
    } while (static_cast<mailbox_t *> (_mailbox)->reschedule_if_needed ());
}

// 새 함수: I/O 스레드 완전 종료 대기
void socket_base_t::wait_async_quiesced (int timeout_ms_)
{
    if (!_async_mailbox_active.load (std::memory_order_acquire)
        && _async_processing_done.load (std::memory_order_acquire))
        return;

    scoped_lock_t lock (_async_done_mu);
    while (!_async_processing_done.load (std::memory_order_acquire))
        _async_done_cv.wait (&_async_done_mu, timeout_ms_);
}
```

spot_sub 측:

```cpp
// spot_sub.cpp destroy_internal에서
if (has_handler && socket && socket->sub_dispatch_active ())
    socket->sub_dispatch_stop ();
// ★ I/O 스레드 완전 종료 대기
if (socket)
    socket->wait_async_quiesced (2000);
```

### 방향 B: close() 내부에서 async quiesce 강제 (더 안전)

`socket_base_t::close()` 자체가 async mailbox가 활성인 경우
자동으로 quiesce를 수행:

```cpp
int socket_base_t::close ()
{
    // ★ async mailbox가 활성이면 먼저 중지하고 대기
    if (_async_mailbox_active.load (std::memory_order_acquire)) {
        stop_async_mailbox_processing ();
        // I/O 스레드가 종료할 때까지 bounded wait
        // (이미 종료됐으면 즉시 반환)
        wait_async_quiesced (2000);
    }

    if (_mailbox)
        static_cast<mailbox_t *> (_mailbox)->clear_signalers ();
    _tag = 0xdeadbeef;
    send_reap (this);
    return 0;
}
```

이 방향이 더 안전한 이유:
- spot 코드뿐 아니라 **모든 async dispatch 사용자**가 자동으로 보호됨
- 호출자가 `wait_async_quiesced()`를 잊어도 `close()`가 처리

### 방향 C: start_reaping()에서 async 완료 보장 (방어적)

방향 B가 적용되지 않는 경우의 safety net:

```cpp
void socket_base_t::start_reaping (poller_t *poller_)
{
    // ★ async handler가 아직 활성이면 대기
    if (_async_mailbox_active.load (std::memory_order_acquire)) {
        stop_async_mailbox_processing ();
        // bounded wait
        for (int i = 0; i < 1000; ++i) {
            if (_async_processing_done.load (std::memory_order_acquire))
                break;
            usleep (1000);
        }
    }

    _poller = poller_;
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->set_io_context (&_poller->get_io_context (),
                             &socket_base_t::reaper_mailbox_handler, this,
                             &socket_base_t::reaper_mailbox_pre_post);
    mailbox->schedule_if_needed ();
    terminate ();
    check_destroy ();
}
```

---

## 5. teardown 순서 개선 (보조 수정)

I/O 스레드 quiesce와 별개로, spot node의 teardown 순서도 개선 필요:

```cpp
// 현재 순서 (spot_node.cpp destroy())
destroy_handles ();          // attachment 소켓 close (data plane 실행 중)
_runtime->stop_and_join ();  // data plane 정지 + join

// 개선 순서
_runtime->stop_and_join ();  // data plane 먼저 정지
destroy_handles ();          // 그 후 attachment close (경합 없음)
```

data plane을 먼저 정지하면:
- ingress/fanout 소켓이 이미 close됨
- attachment pipe의 peer가 이미 reaper에 있음
- pipe 명령이 순차적으로 처리됨

단, `stop_and_join()` 전에 "terminate" ctrl 명령을 보내는 경로를 확인해야 함.
현재 `stop_and_join()`은 `stop.set(1)` → `stop_sockets()` → join → `close_control_sockets()`
순서인데, data plane의 poll loop가 정상적으로 빠져나오려면
ctrl에 "terminate"을 보내거나 socket stop으로 poll을 중단시켜야 함.

---

## 6. 요약

| 문제 | 원인 | 해결 |
|------|------|------|
| 1차: socket tracking 유실 | `wait_drained()` swap 후 유실 | **수정 완료** (73dfc80f) |
| 2차: `_term_acks` assertion | I/O 스레드 vs reaper data race | async quiesce 보장 필요 |
| 구조적 | 소켓 상태가 단일 스레드 접근 가정 | `close()` 전 async 완전 정지 |

**즉시 적용 권장**: 방향 B (close() 내부에서 async quiesce 강제)
- 모든 async dispatch 사용자에게 자동 적용
- 호출 순서 실수를 방어
- 최소 침습적 변경

**보조 수정**: teardown 순서 변경 (stop_and_join 먼저)
- data plane 경합 제거
- 독립적으로 적용 가능
