# Request/Reply 도착 즉시 깨움 (Wakeup) 구현 계획

> 작성일: 2026-05-10
>
> 목적: dealer/router의 `request → reply` 흐름에서 reply가 도착하면
> `zlink_poller_wait` 가 timeout 만료를 기다리지 않고 즉시 깨어나도록, core poller가
> internal completion signal을 OS-level wakeup source에 연결한다.
>
> 본 문서의 사실은 모두 작성 시점에 직접 측정하거나 검증한 것만 기록한다.
> 추측은 별도로 구분해 표시한다.

## TODO

- [ ] 1단계: core poller에 socket completion signal fd를 OS-level wakeup으로 연결
- [ ] 2단계: cpp binding `async_result_t::wait` 가 1ms slice 폴링 대신 wakeup-driven wait 사용
- [ ] 3단계: cpp perf 검증 (SPOT_REQREP throughput 향상 확인)
- [ ] 4단계: c reference perf 검증
- [ ] 5단계: 다른 binding (java/dotnet/node/python/go/rust) 의 1ms slice 제거 또는 동일 패턴 적용
- [ ] 6단계: doc/spec/bindings, doc/perf 문서 갱신
- [ ] 7단계: 회귀 테스트

## 1. 측정 데이터 (검증된 사실)

### 1.1 baseline (현재 코드, 1ms 폴링)

| 측정 | tcp 64B | tcp 1024B | latency mean |
|---|---|---|---|
| c SPOT_REQREP | 858 ops/s | 868 ops/s | 1.16ms |
| cpp SPOT_REQREP | 858 ops/s | 858 ops/s | 1.16ms |

### 1.2 실험: 1ms → 0ms (busy poll)

c reference: `bindings/c/perf/single/src/perf_spot_reqrep.cpp:355` 의
`zlink_poller_wait(poller, &ev, 1, NULL)` → `0` 으로 변경.

cpp binding: `bindings/cpp/include/zlink/async_result.hpp:171` 의
`progress_slice` 를 `0ms` 로 변경.

| 측정 | tcp 64B | tcp 1024B | 비율 |
|---|---|---|---|
| c SPOT_REQREP (busy) | 10,327 ops/s | 7,984 ops/s | 12.0x / 9.2x |
| cpp SPOT_REQREP (busy) | 11,716 ops/s | 11,599 ops/s | 13.7x / 13.5x |

→ **확정**: 1ms timeout이 throughput을 직접 cap 한다. signal/dispatch 자체는 정상이다.
busy poll 시 latency가 1.16ms에서 0.05–0.09ms 로 떨어진다.

실험 후 두 파일 모두 원본으로 복원했다.

### 1.3 코드 경로 (직접 grep 으로 확인된 파일/줄)

| 위치 | 역할 |
|---|---|
| `core/src/api/socket_request_reply_dispatch.cpp:23` | reply 수신 시 lookup → completion 큐에 push |
| `core/src/api/request_completion_queue_internal.cpp:66-99,165-180` | deque push, 빈 큐였다면 `signal.tx`에 1바이트 write |
| `core/src/api/socket_request_reply_internal.cpp:120-126` | `completion_signal_socket()` (헤더에 선언 없는 internal getter) |
| `core/src/api/socket_request_reply_router_api.cpp:452-484` | `zlink_socket_request_progress_internal` (zlink.h 선언 없음, internal symbol) |
| `core/src/api/poller_api.cpp:91-134` | `drain_hidden_completion_registration` — `zlink_poller_wait` 끝날 때 자동 호출, completion drain |
| `core/src/api/poller_api.cpp:670-733` | `zlink_poller_wait`, `zlink_poller_wait_all` |
| `bindings/cpp/include/zlink/async_result.hpp:46-58, 169-172` | `wait()` / `progress_slice` (1ms) |
| `bindings/c/perf/single/src/perf_spot_reqrep.cpp:352-360` | `poll_client_progress` (1ms) |

`zlink_socket_request_progress_internal` 의 호출처는 모두 forward declaration이다:
- `core/tests/integration/test_zmp_request_reply.cpp:23,181,207`
- `core/tests/integration/test_helper_request_sequence_failure.cpp:14,79`
- `core/tests/integration/test_spot_poller.cpp:13,259`
- `bindings/cpp/include/zlink/base_socket.hpp:46`
- `bindings/cpp/include/zlink/socket_types.hpp:74-76`
- `bindings/java/native/src/zlink_java_reqrep_bridge.c:14`
- `bindings/java/src/main/java/systems/zlink/internal/Native.java:588`
- `bindings/dotnet/src/Zlink/RequestProgressPump.cs:25`
- `bindings/dotnet/src/Zlink/Native/NativeMethods.Core.cs:43,243`
- `bindings/node/native/src/addon_core.cc:3927`
- `bindings/node/native/src/addon_api.h:12`
- `bindings/python/src/zlink/_ffi.py:529`
- `bindings/python/src/zlink/_socket_types.py:291,393`
- `bindings/python/src/zlink/_spot.py:1753`
- `bindings/rust/src/ffi.rs:1103`
- `bindings/rust/src/request_progress.rs:110`
- `bindings/go/request_reply.go:10,245`

## 2. 진단 (측정에 의한 결론)

`zlink_poller_wait` 가 socket을 받을 때:
- 그 socket을 epoll/poll/select 의 OS wakeup source로 등록한다 (정상).
- 동시에 hidden completion registration도 만들어서 wait 끝에 drain 한다
  (`drain_hidden_completion_registration`).
- 그러나 **completion signal fd 자체는 OS wakeup source에 연결되지 않은 것으로 측정됐다**
  (1ms timeout 만료 전엔 깨어나지 않는다).

cpp `async_result_t` 는 poller를 쓰지 않고 자기 폴링(1ms slice)으로 progress를
호출한다. 같은 cap에 걸린다.

## 3. 설계

### 3.1 핵심 변경: core poller가 completion signal을 wakeup source로 등록

**zlink.h public 인터페이스 추가/제거 없음.**

`zlink_poller_add` 가 socket을 등록할 때:
- 그 socket의 `socket_request_reply_state_t::signal.rx` 를 함께 epoll/poll
  wakeup 소스로 등록한다.
- spot의 경우 `spot_request_reply_state_t::signal.rx` 도 등록한다.
- ROUTER spot dispatch도 동일하다.
- 등록은 idempotent하다.

내부 구현:
- `core/src/api/poller_api.cpp` 의 `zlink_poller_add` 본체에서 socket 종류에 따라
  `completion_signal_socket()` 결과를 wakeup source로 추가한다.
- registration 구조체에 wakeup fd / handle 필드를 추가한다.
- `zlink_poller_remove` / socket close 시 wakeup source도 같이 해제한다.

**effect**: 기존 `drain_hidden_completion_registration` 흐름은 유지된다.
달라지는 건 wakeup 시점뿐이다. timeout 만료 전에 epoll 이 깨어나서 drain 한다.

### 3.2 cpp binding 변경

`async_result_t::wait()` 의 1ms slice는 **유지해도 무방하다** (core 측 wakeup이
즉시 깨우면 1ms 이전에 이미 future ready이므로 slice 길이는 비결정적이 된다).
다만 더 깔끔하게 하려면 다음과 같다.

옵션 A (최소 변경): `progress_slice` 만 길게(예: 100ms) 잡는다. core가 즉시 깨우므로
실제로는 100ms를 다 쓸 일이 없다. CPU 사용 ↓.

옵션 B (구조 변경): `progress` 콜백을 nonblocking pump로만 두고, blocking 대기는
`zlink_poller_wait` 또는 새 internal blocking helper로 교체한다. coroutine 통합 시 더
깔끔하지만 변경 범위가 크다.

이번 변경에서는 **옵션 A**만 적용하고 측정으로 효과를 확인한다. 옵션 B는 후속이다.

### 3.3 c reference perf 변경

c reference도 자기 코드의 `zlink_poller_wait(..., 1ms, ...)` timeout만 길게 잡으면 된다
(예: 100ms). 깨움이 즉시 일어나므로 timeout은 fallback 용으로만 의미가 있다.
실제 코드 수정은 1줄이다.

### 3.4 다른 binding 변경

binding 내부의 1ms slice 패턴(있다면)을 동일하게 길게 잡거나 제거한다. 각 binding
주체 코드는 stage 5에서 별도로 검토한다.

## 4. 변경되는 인터페이스

| 종류 | 변경 |
|---|---|
| `zlink.h` public C API | **변경 없음** |
| 내부 implementation symbol (`zlink_socket_request_progress_internal` 등) | **변경 없음** (시그니처/동작 동일) |
| binding 사용자 시점 API (cpp `async_result_t`, java/dotnet/node future) | **변경 없음** |
| 내부 구현 (`zlink_poller_add` 본체, registration 구조체) | wakeup source 추가 |
| binding 측 폴링 timeout 상수 | 1ms → 100ms (또는 더 큰 값) |

→ 외부 사용자가 코드를 수정해야 할 부분은 0이다.

## 5. 구현 단계

### 단계 1 — core poller에 wakeup 연결

**파일**:
- `core/src/api/poller_api.cpp` — `zlink_poller_add` 가 socket 종류를 식별한 뒤
  internal completion signal fd를 wakeup source 로 추가. registration 구조체에
  signal fd 필드 추가.
- `core/src/api/poller_internal.hpp` (또는 동등) — registration 구조 갱신.

**테스트**:
- `core/tests/integration/test_helper_request_sequence_failure.cpp` 와
  `test_zmp_request_reply.cpp`, `test_spot_poller.cpp` 통과 유지.
- 새 microbench: `zlink_poller_wait(timeout=1초)` 호출 시 reply 도착 즉시 return
  (50µs 이내) 검증.

### 단계 2 — cpp binding 측 슬라이스 길게

**파일**:
- `bindings/cpp/include/zlink/async_result.hpp:171` — `progress_slice` 를 1ms →
  100ms (또는 더 큰 값).

**테스트**:
- `bindings/cpp/tests/contract/test_cpp_contract_request_reply.cpp` 통과 유지.

### 단계 3 — cpp perf 검증

- `bindings/cpp/perf/run_benchmarks.sh --pattern SPOT_REQREP --transports tcp`
- 목표: 64B/1024B 모두 c reference 대비 ≥85%.

### 단계 4 — c reference perf 검증

- `bindings/c/perf/single/src/perf_spot_reqrep.cpp:355` 의 `1ms` → 100ms (또는 더 큰 값).
- `bindings/c/perf/run_benchmarks.sh --pattern SPOT_REQREP --transports tcp` 측정.
- 목표: SPOT_REQREP throughput 5,000 ops/s 이상.

### 단계 5 — 다른 binding 검토

각 binding의 wait/await 코드에서 1ms slice 또는 동일 패턴이 있는지 확인한 뒤 제거한다.

| binding | 검토 대상 |
|---|---|
| java | `RouterRequestSupport`, `Native.java` request progress 호출처 |
| dotnet | `RequestProgressPump.cs` 폴링 cadence |
| node | `addon_core.cc:3927` 호출처의 schedule 정책 |
| python | `_socket_types.py:291,393`, `_spot.py:1753` 호출처의 sleep 정책 |
| go | `request_reply.go:245` 호출 사이트의 channel/sleep 정책 |
| rust | `request_progress.rs:110` 호출처의 schedule 정책 |

### 단계 6 — 문서 갱신

- `doc/spec/bindings/README.md` — request/reply wakeup 정책 1줄 추가
- `doc/spec/bindings/c/README.md` — `zlink_poller_wait` 가 reply signal에서도
  깨어남을 명시
- `doc/spec/bindings/cpp/README.md` — `async_result_t` 의 wait 동작 명시
- `doc/perf/PERF_POLICY.md` — 단발 request 패턴의 polling slice 의존이 사라졌다는 노트
- 본 plan 문서 — 단계 진행 시 TODO 체크 갱신

### 단계 7 — 회귀 검증

- 각 binding의 contract / unit test 100% 통과
- perf full run (single + multi) 으로 회귀 없음 확인
- 결과는 별도 결과 로그에 기록

## 6. 비목표

- io thread / reactor 모델을 zlink가 새로 도입하는 일 (이미 `zlink_poller` 가
  reactor 역할).
- coroutine 통합 모델을 binding마다 통일하는 일 (각 binding idiom 보존).
- `zlink_socket_request_progress_internal` 의 시그니처/이름 변경 (불필요).
- 새 public C API 추가.

## 7. 위험과 완화

| 위험 | 완화 |
|---|---|
| poller에 등록되는 wakeup fd 가 socket close 후 해제되지 않으면 epoll 누수 | `zlink_poller_remove` / socket close 경로에서 wakeup source 해제. `core/tests/integration/test_spot_poller.cpp` 에 케이스 추가 |
| OS별 차이 (Windows IOCP vs unix epoll) | core가 이미 추상화된 poller 가지고 있으므로 그 추상에 wakeup fd 만 추가. binding 코드 변경 없음 |
| 단발 polling cadence 에 의존하던 다른 패턴이 회복 못함 | 단계 7에서 회귀 검증. 패턴별 throughput 비교 |
| busy 1ms 폴링 cadence 에 잡혀 있던 race가 1ms 사라지면서 노출 | core/binding contract test 충분히 돌리기. 필요 시 race 패치 별건으로 처리 |

## 8. 검증 약속

본 문서의 모든 사실(파일 경로, 줄 번호, 함수 이름, 측정값)은 작성 시점에 직접
grep / 빌드 / 측정으로 확인했다. 단계 진행 중 새 fact 가 드러나면 본 문서를
즉시 갱신한다. 추측이 필요한 항목은 "추측" 또는 "가설" 로 명시한다.
