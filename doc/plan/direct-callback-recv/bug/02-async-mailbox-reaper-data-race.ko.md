# BUG-02: async mailbox와 reaper 간 data race

상태: **분석 완료, 수정 대기**

---

## 증상

BUG-01 수정 후 stress 반복에서 새로 드러난 문제:

1. split ctest 반복 중 `test_spot_service_introspection_explicit_handles` 60초 timeout
2. 단독 반복 중 `Assertion failed: _term_acks > 0 at own.cpp:153`

핵심 특성:
- TLS, monitor, TCP 없는 **순수 inproc** 테스트에서도 재현
- 반복 stress에서만 비결정적으로 발생
- BUG-01 수정으로 소켓 추적이 개선되면서 reaper가 더 빠르게 동작 → 이 race window가 더 자주 열림

---

## 원인

### 배경: direct callback의 async mailbox 구조

`spot_sub_t::set_direct_handler()` → `sub_dispatch_start()` 호출 시,
SUB attachment 소켓의 mailbox가 **I/O 스레드**에서 처리되도록 등록된다.

```
start_async_mailbox_processing(io_thread):
  _async_mailbox_active = true
  mailbox->set_io_context(io_thread의 ASIO context,
                          async_mailbox_handler, ...)
```

이후 소켓의 모든 명령(pipe_term, pipe_term_ack 포함)이
I/O 스레드의 `process_async_mailbox()` → `process_commands()`에서 처리된다.
이 과정에서 `_pipes`, `_term_acks` 등 소켓 내부 상태가 변경된다.

### data race 발생 경로

```
Application thread          I/O thread                Reaper thread
─────────────────          ──────────                ─────────────
sub_dispatch_stop()
  _async_mailbox_active=false
  schedule_if_needed()
  ← 즉시 반환 ★
                           process_async_mailbox():
                             process_commands()
                               pipe_term_ack 수신
                               → pipe_terminated()
                               → _pipes.erase(P)   ←── ★ _pipes 변경 중
                             _async_mailbox_active?
                             → false
                             → io_context 해제

close_socket()
  stop() + close()
  → reap 명령 전송
                                                     start_reaping():
                                                       set_io_context(reaper)
                                                       terminate()
                                                       → process_term()
                                                         _pipes 순회    ←── ★ 동시 접근!
                                                         register_term_acks()
```

### 왜 동시 접근이 발생하는가

**`stop_async_mailbox_processing()`이 I/O 스레드 완료를 기다리지 않는다:**

```cpp
// socket_base.cpp:1770-1775
void socket_base_t::stop_async_mailbox_processing ()
{
    _async_mailbox_active.store (false, std::memory_order_release);
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->schedule_if_needed ();
    // ← 즉시 반환. I/O 스레드가 process_commands() 중일 수 있음
}
```

**`start_reaping()`이 이전 I/O handler 완료를 확인하지 않는다:**

```cpp
// socket_base.cpp:1686-1701
void socket_base_t::start_reaping (poller_t *poller_)
{
    _poller = poller_;
    mailbox->set_io_context (reaper_context, ...);
    // ← 이전 I/O handler가 아직 process_commands() 실행 중이어도 진행
    terminate ();     // ← process_term()이 _pipes를 순회
}
```

### 경합 대상 자원

| 자원 | I/O 스레드 (async handler) | Reaper 스레드 |
|------|--------------------------|--------------|
| `_pipes` | `pipe_terminated()` → erase | `process_term()` → iterate + terminate |
| `_term_acks` | `unregister_term_ack()` (간접) | `register_term_acks()` |
| `_terminating` | `is_terminating()` 읽기 | `process_term()` → true 설정 |
| pipe 상태 | `process_pipe_term_ack()` → delete | `pipe->terminate()` |

이것은 `_pipes`와 `_term_acks`에 대한 **동기화 없는 동시 접근**(data race)이며,
결과:
- `_pipes.size()`가 부정확 → `register_term_acks()` 과다/과소 등록
- 이미 정리된 pipe에 대한 `pipe_terminated()` → `unregister_term_ack()` 초과 호출
- `_term_acks == 0`인데 `unregister_term_ack()` → **assertion 실패**

### 왜 stress에서만 재현되는가

- 단일 실행: I/O 스레드가 flag를 빠르게 확인하고 종료
- stress 반복: I/O 스레드에 pending 명령이 많거나 스케줄링 지연
  → `process_commands()` 실행 시간 증가
  → `close_socket()` → reaper 시작 시점에 I/O 스레드가 아직 활성

---

## 왜 이것이 구조적 문제인가

소켓 내부 상태(`_pipes`, `_term_acks`, `_terminating`)는
**단일 스레드에서만 접근된다는 암묵적 가정**에 의존한다.
ZMQ/zlink의 원래 소켓 모델에서는 이 가정이 성립했다:

- API 호출: application thread (close 전까지)
- reaper 처리: reaper thread (close 후)
- 두 시점은 겹치지 않음

그러나 direct callback의 `sub_dispatch_start()`가 소켓 mailbox를
I/O 스레드에 등록하면서 이 가정이 **무효화**된다:

- I/O 스레드: `process_commands()` → pipe 명령 처리 → `_pipes` 변경
- Reaper 스레드: `process_term()` → `_pipes` 순회 + `_term_acks` 등록
- 두 접근이 **동시에** 발생할 수 있음

이것이 "하나를 고치면 다른 문제가 나오는" 근본 원인이다.
개별 수정(tracking 보존, term_endpoint 제거)은 증상을 일부 완화하지만,
data race 자체를 제거하지 않으면 비결정적 실패가 반복된다.

---

## 구조적 맥락: 이중 종료 모델

async mailbox race는 **직접 트리거**이지만,
이것이 반복적으로 문제를 일으키는 배경에는 더 깊은 구조적 이유가 있다.

### spot과 core의 이중 종료 판정

spot 계층(`service_runtime_base_t`)과 core 계층(`own_t`/`socket_base_t`)이
각각 독립적으로 종료 완료를 판정한다:

- **spot**: `wait_drained()` / `force_wait_remaining()` → "tracked socket이 다 사라졌는가"
- **core**: `check_term_acks()` → "pipe termination + owned ack가 모두 도착했는가"

두 판정이 어긋나면 timeout 또는 assertion으로 드러난다.
async mailbox race는 이 어긋남을 트리거하는 직접 경로이지만,
data plane 활성 중 attachment close, `term_endpoint()` 선호출 등
**다른 경로로도 같은 계열의 불일치가 발생할 수 있다.**

### `_term_acks` 혼합 카운터 문제

`own_t::_term_acks`가 pipe completion과 owned-object completion을
같은 카운터로 세고 있어서, 어느 쪽에서 불일치가 생겼는지 구분이 어렵다.

장기적으로는 pipe termination 상태를 `socket_base_t` 전용으로 분리하여
`own_t::_term_acks`는 owned-object ack만 담당하게 하는 것이 맞다.

→ 상세: [구조 개선안](codex-spot-shutdown-structural-rework.ko.md),
  [lifecycle contract 분석](deterministic-lifecycle-root-cause-review.ko.md)

---

## 기각된 가설: term_endpoint ack 불일치

`term_endpoint()` + `close()` 시퀀스의 pipe ack 불일치를
가설로 분석했으나, `term_endpoint()` 제거 후에도
동일 증상이 재현되어 기각되었다.

다만 `term_endpoint()` 선호출 자체는 teardown window를 넓히는
**경합 증폭기** 역할을 하므로, 구조 개선 시 제거 대상이다.
이유:
- 어차피 socket close가 core termination graph를 타며 pipe를 정리
- handle이 peer internal socket보다 먼저 pipe 상태를 건드리면
  data plane 쪽 teardown window가 불필요하게 넓어짐

---

## 해결 방향

### 방향 A: close() 내부에서 async mailbox quiesce 강제 (권장)

소켓의 `close()`가 호출될 때, async mailbox가 활성이면
I/O 스레드가 완전히 빠져나올 때까지 대기한 후 reaper로 전송한다.

```cpp
int socket_base_t::close ()
{
    // ★ async mailbox가 활성이면 먼저 중지하고 완료 대기
    if (_async_mailbox_active.load (std::memory_order_acquire)) {
        stop_async_mailbox_processing ();
        wait_async_quiesced (2000);
    }

    if (_mailbox)
        static_cast<mailbox_t *> (_mailbox)->clear_signalers ();
    _tag = 0xdeadbeef;
    send_reap (this);
    return 0;
}
```

장점:
- 모든 async dispatch 사용자(sub, xpub, stream)에 자동 적용
- 호출 순서 실수를 방어
- 최소 침습적 변경

필요한 추가 구현:
- `_async_processing_done` atomic + condition variable
- `process_async_mailbox()` 종료 시 알림
- `wait_async_quiesced()` bounded wait 함수

### 방향 B: start_reaping()에서 async 완료 보장 (방어적 보조)

방향 A의 safety net. `start_reaping()`에서도 확인:

```cpp
void socket_base_t::start_reaping (poller_t *poller_)
{
    // ★ async handler가 아직 활성이면 대기
    if (_async_mailbox_active.load (std::memory_order_acquire)) {
        stop_async_mailbox_processing ();
        wait_async_quiesced (1000);
    }

    _poller = poller_;
    // ... (기존 로직)
}
```

### 방향 C: teardown 순서 개선 (보조)

data plane을 먼저 정지하여 pipe 경합 자체를 줄인다:

```cpp
// 현재
destroy_handles ();          // data plane 실행 중에 attachment close
_runtime->stop_and_join ();  // 그 후 data plane 정지

// 개선
_runtime->stop_and_join ();  // data plane 먼저 정지 (pipe peer 안정화)
destroy_handles ();          // 그 후 attachment close (경합 감소)
```

이 변경은 data race 자체를 제거하지는 않지만,
race window를 줄여 재현 빈도를 낮춘다.

---

## 구현 상세

### wait_async_quiesced 메커니즘

```cpp
// socket_base.hpp에 추가
std::atomic<bool> _async_processing_done{true};
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

// process_async_mailbox() 수정 — 종료 시 알림 추가
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
            // ★ 종료 알림
            _async_processing_done.store (true, std::memory_order_release);
            {
                scoped_lock_t lock (_async_done_mu);
                _async_done_cv.broadcast ();
            }
            return;
        }
    } while (static_cast<mailbox_t *> (_mailbox)->reschedule_if_needed ());
}

// 새 함수
void socket_base_t::wait_async_quiesced (int timeout_ms_)
{
    if (_async_processing_done.load (std::memory_order_acquire))
        return;
    scoped_lock_t lock (_async_done_mu);
    while (!_async_processing_done.load (std::memory_order_acquire))
        _async_done_cv.wait (&_async_done_mu, timeout_ms_ > 0 ? timeout_ms_ : 1000);
}
```

### 디버그 계측 (선택)

```cpp
#ifdef ZLINK_DEBUG_TERM_ACKS
void own_t::register_term_acks (int count_)
{
    fprintf (stderr, "[term-acks] register %d (was %d) at %p\n",
             count_, _term_acks, this);
    _term_acks += count_;
}

void own_t::unregister_term_ack ()
{
    fprintf (stderr, "[term-acks] unregister (was %d) at %p\n",
             _term_acks, this);
    zlink_assert (_term_acks > 0);
    _term_acks--;
    check_term_acks ();
}
#endif
```

---

## 관련 파일

| 파일 | 역할 |
|------|------|
| `core/src/sockets/socket_base.cpp` | `close()`, `start_reaping()`, `process_async_mailbox()`, `stop_async_mailbox_processing()` |
| `core/src/sockets/socket_base.hpp` | `_async_mailbox_active`, `_mailbox_refcnt` |
| `core/src/sockets/xsub.cpp` | `sub_dispatch_start()`, `sub_dispatch_stop()` |
| `core/src/core/own.cpp:153` | assertion 위치: `unregister_term_ack()` |
| `core/src/core/pipe.cpp` | `terminate()`, `process_pipe_term()`, `process_pipe_term_ack()` |
| `core/src/services/spot/spot_sub.cpp` | `destroy_internal()` — `sub_dispatch_stop()` + `close_socket()` 시퀀스 |
| `core/src/services/spot/spot_node.cpp` | `destroy()` — teardown 순서 |

---

## 수정 우선순위

### 즉시 적용 (직접 트리거 차단)

| 순위 | 수정 | 효과 |
|------|------|------|
| 1 | `close()` 내 async quiesce | data race 원천 제거 |
| 2 | `start_reaping()` 방어적 체크 | safety net |
| 3 | teardown 순서 변경 (quiesce-first) | race window 축소 |
| 4 | `_term_acks` 디버그 계측 | 재현 시 추적 지원 |

### 구조 개선 (같은 계열 반복 방지)

| 순위 | 수정 | 효과 |
|------|------|------|
| 5 | pipe ack를 `own_t::_term_acks`에서 분리 | ack 불일치 원인 격리 |
| 6 | spot의 종료 완료 판정을 core에 위임 | 이중 모델 해소 |
| 7 | `term_endpoint()` 선호출 제거 | 경합 증폭기 제거 |
| 8 | destroy strict fail-fast | ctx hang 방지 |

→ 구조 개선 상세: [codex-spot-shutdown-structural-rework.ko.md](codex-spot-shutdown-structural-rework.ko.md)
