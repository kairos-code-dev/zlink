# `bindings/cpp/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B** — 직전 리팩토링으로 native API 제거/shadow path 정리 완료,
> 남은 문제는 single/multi 간 중복과 callback/recv 이중 구현
> 대상: `bindings/cpp/perf/`

## 1. 목표

C++ perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- single/multi 간 중복 유틸리티(TLS, latency sampler, monitor wait)가 `common/`으로 통합된다.
- callback/recv 모드 전환의 변경 증폭이 줄어든다.
- callback hot path에서 lock 비용이 측정에 섞이지 않는다.
- socket/context guard 같은 공통 타입이 한 곳에서 정의된다.
- `core/perf`와 동일한 측정 의미를 유지한다.
- **매 단계 완료 시 smoke perf 실행 + `core/perf/baseline` 대비 regression 확인**
  (throughput/latency 5% 초과 악화 시 해당 단계 변경 원복)

## 2. 현재 상태 요약

직전 리팩토링(커밋 98a59e20)으로 해결된 항목:
- direct native API (`zlink_*`) 호출 → 0건 (zlink:: C++ binding API만 사용)
- STREAM callback shadow path → 제거, canonical `--recv` 경로 통합
- dead target (`cpp_comp_src_stream_callback_server`) → 삭제
- single SPOT send model → 정책 정렬 완료

## 3. 현재 남은 POSD 문제

### 3.1 TLS 인증서 해석 중복 (40줄)

- `single/common/perf_single_tls.hpp` (21-66줄)
- `multi/common/perf_tls.hpp` (21-63줄)
- 동일한 파일시스템 탐색 로직 (bindings/cpp/tests/certs/gen, /proc/self/exe 기준)
- 차이: single은 실패 시 stderr 출력, multi는 silent return

### 3.2 latency sampler 중복 (100줄)

- single: `latency_stats_builder_t` (선언/구현 분리, .hpp + .cpp)
- multi: `bench_latency_sampler_t` (inline, `merge_from()` 기능 추가)
- 동일한 reservoir sampling 알고리즘, 동일한 RNG seed (`0x9e3779b97f4a7c15ULL`)
- 동일한 percentile 계산

핵심 차이는 multi 버전에 `merge_from()`이 있다는 것뿐.
latency 계산 수정 시 양쪽 모두 수정 필요.

### 3.3 monitor event wait 중복 (100줄, 의미 차이 숨김)

- single: `wait_socket_monitor_event()` (perf_single_common.cpp 390-500줄)
  - 값 비교: `event->value != value_` (정확 일치)
- multi: `wait_socket_monitor_event()` (perf_common.hpp 341-397줄)
  - 값 비교: `event->value >= min_value` (최소 임계값)

95% 동일한 코드에 **미묘한 시맨틱 차이가 숨겨져 있음**.
이 차이가 의도적인지 버그인지 코드에서 드러나지 않음.

### 3.4 callback/recv 이중 구현으로 인한 바이너리 팽창

- `perf_spot_client.cpp` (1025줄), `perf_router_router_client.cpp` (657줄) 등
  각 multi client에 callback과 recv 두 모드가 모두 포함
- `if (multi_perf_callback_mode())` 런타임 분기
- callback 전용 헬퍼: `perf_spot_client_callback.hpp`, `perf_spot_client_recv.hpp`
- latency 관련 변경 시 callback 헬퍼와 recv 헬퍼 양쪽 수정 필요

### 3.5 callback hot path locking

- `single/common/perf_single_common.cpp` (772-1099줄): `_queue_mutex` + `_result_mutex` 2-lock 설계
- callback handler가 zlink callback context에서 `_queue_mutex` 획득
- lock 경합 시 latency 측정에 lock 대기 시간이 포함될 수 있음

### 3.6 socket/context guard 중복 정의

- `socket_guard_t`, `ctx_guard_t`가 single/common과 multi/common에 각각 정의
- `common/perf_socket_compat.hpp`로 이동 가능

### 3.7 불필요한 drain/settle 인프라 (정책 위반)

`core/perf` C 소스에는 `phase_drain`도 `settle`도 없다.
정책은 phase를 `ready → warmup → active`로만 허용하고 새 phase 추가를 금지한다.
사이즈별 서버/클라이언트 프로세스가 재시작되므로 잔여 메시지 drain은 불필요하다.

현재 C++ binding perf에만 존재하는 drain/settle 코드:

| 항목 | 위치 |
|------|------|
| `phase_drain = 3` enum | `perf_metric_header.hpp:17` |
| `settle()` 함수 | single `perf_single_common.cpp:354`, multi `perf_common.hpp:615` |
| `PERF_SETTLE_MS` env var (500ms) | `perf_common_multi.hpp:165` |
| `drain_warmup_replies()` | `perf_dealer_router_client.cpp:375`, `perf_router_router_client.cpp:492` |
| `drain_recv()` | `perf_spot_client.cpp:753` |
| `drain_count` 카운터 | `perf_pubsub_client.cpp:34` |
| `run_phase(phase_drain, ...)` 호출 | `perf_dealer_dealer_client.cpp:110`, `perf_pubsub_client.cpp:89` |
| `phase_drain` 사용하는 `try_send_request` | `perf_dealer_router_client.cpp:404,437`, `perf_router_router_client.cpp:195,225,252,521,557` |
| pubsub server drain phase send | `perf_pubsub_server.cpp:181` |

### 3.8 metric header magic 차이 미문서화

- single: `0x53504631` ("SPF1")
- multi: `0x4D504631` ("MPF1")
- 의도적 구분이지만 코드에 설명 없음

## 4. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- C++ 관용 스타일 유지 (RAII, typed wrapper, value type, 명시적 ownership)
- 공통화는 변경 증폭 축소가 목적
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약 준수:
  - single: callback only (전 패턴, 예외 없음)
  - multi: recv only (SPOT/STREAM만 dual-mode)
  - 미지원 조합은 fail-fast
- **inline 코드 요구사항** (`PERF_POLICY.md` 715-727줄):
  패턴 파일에서 send/recv API 호출, EAGAIN 처리, recv/callback 모델 선택,
  ready gate, 소켓 생성이 직접 보여야 한다. 공통화 시 이 로직을 helper 뒤에
  숨기지 않는다. 설정/TLS/결과 출력은 공통화 가능.
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

## 5. 단계별 실행 계획

### 단계 0. 현황 동결

할 일:
- single/multi 간 중복 함수 대응표 (TLS, latency, monitor wait, guard)
- monitor wait의 값 비교 시맨틱 차이가 의도적인지 확인
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 통합/이동 대상이 함수 단위로 정리됨
- monitor wait 시맨틱 차이의 의도 확인됨

### 단계 1. phase drain/settle 삭제 (recv drain loop는 유지)

**중요 구분**: 정책에서 "drain"은 두 가지 의미로 사용된다.
- **recv drain loop** = POLLIN readiness 후 nonblocking recv until EAGAIN.
  이것은 recv 모델의 핵심 측정 메커니즘이며 **삭제하면 안 된다**.
- **phase drain** = warmup→active 사이 별도 phase (`phase_drain = 3`, `settle()`,
  `drain_warmup_replies()` 등). 이것은 정책이 금지하는 pseudo-phase이며 **삭제 대상**.

할 일:
- `perf_metric_header.hpp`에서 `phase_drain = 3` enum 삭제
- `settle()` 함수 삭제 (single `perf_single_common.cpp:354`, multi `perf_common.hpp:615`)
- `PERF_SETTLE_MS` env var 해석 삭제 (`perf_common_multi.hpp:165`)
- 모든 `settle_ms` 필드, `drain_count` 필드 삭제
- `drain_warmup_replies()` 함수 삭제 (`perf_dealer_router_client.cpp:375`,
  `perf_router_router_client.cpp:492`) — 이것은 warmup→active 전환 시
  잔여 reply를 별도 phase로 제거하는 코드이며, recv drain loop가 아님
- `run_phase(phase_drain, ...)` 호출부 삭제
- `phase_drain`을 사용하는 `try_send_request` 호출부 삭제
- pubsub server의 drain phase send 삭제 (`perf_pubsub_server.cpp:181`)
- `settle()` 호출부 삭제 (`perf_spot.cpp:127`, `perf_pubsub.cpp:86` 등)
- **유지해야 할 것**: recv 모델의 POLLIN→nonblocking recv loop (`drain`이라는
  이름이 붙어 있어도 실제로 poller event loop 안의 recv 루프이면 유지)
- `spot_client.cpp`의 `drain_recv()` (753줄)은 recv drain loop인지
  phase drain인지 확인 후 판단:
  - poller POLLIN에서 호출되는 nonblocking recv → **유지** (이름만 변경 가능)
  - warmup→active 전환 전용 별도 호출 → **삭제**
- 삭제 전 각 drain 함수에 대해 **warmup phase에서 보낸 메시지가 active phase
  latency에 혼입되는지** 확인. 혼입 위험이 있으면 phase 전환 시 header의
  phase 필드로 걸러지는지 확인 (정책: phase/magic 불일치 메시지는 집계 제외)

phase 구조는 `core/perf`와 동일하게 `ready → warmup → active`만 남긴다.

완료 기준:
- `phase_drain` enum grep 0건
- `settle()` 함수/변수/env var grep 0건
- `drain_warmup_replies` / `drain_count` grep 0건
- recv 모델의 POLLIN→nonblocking recv loop는 정상 유지
- phase 구조가 `ready → warmup → active`만 존재
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 2. TLS 해석 통합

할 일:
- `common/perf_tls.hpp` 생성
- single/multi 양쪽의 TLS 인증서 탐색 로직을 이 파일로 이동
- verbose 파라미터로 stderr 출력 여부 제어:

```cpp
namespace perf {
bool try_resolve_tls_paths(
    std::string &cert_out, std::string &key_out, std::string &ca_out,
    bool verbose = false);
}
```

- `perf_single_tls.hpp`, `perf_tls.hpp`에서 중복 구현 삭제, 공통 헤더 include

완료 기준:
- TLS 해석 구현 1곳
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 3. latency sampler 통합

할 일:
- `common/perf_latency_sampler.hpp` 생성
- reservoir sampling + percentile 계산 + RNG을 하나의 클래스로 통합
- `merge_from()` 기능 포함 (single에서는 사용 안 해도 존재):

```cpp
namespace perf {
class latency_sampler_t {
public:
    void add(double latency_us);
    void merge_from(const latency_sampler_t &other);
    struct stats_t { double mean, p95, p99; };
    stats_t snapshot() const;
};
}
```

- single의 `latency_stats_builder_t`와 multi의 `bench_latency_sampler_t`를 이 클래스로 교체

완료 기준:
- latency sampling 구현 1곳
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 4. monitor wait 통합

할 일:
- `common/perf_monitor_wait.hpp` 생성
- 단계 0에서 확인한 값 비교 시맨틱에 따라 통합:

```cpp
namespace perf {
enum class value_compare { exact, min_threshold };

template<typename MonitorT>
bool wait_monitor_event(
    MonitorT &monitor, uint64_t event_type,
    int64_t value, value_compare cmp,
    int timeout_ms);
}
```

- single/multi의 각 wait 함수를 이 템플릿 호출로 교체
- 시맨틱 차이가 `value_compare` 파라미터로 명시적으로 드러남

완료 기준:
- monitor wait 구현 1곳
- 시맨틱 차이가 호출부에서 명시적
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 5. socket/context guard 이동 + magic 문서화

할 일:
- `socket_guard_t`, `ctx_guard_t`를 `common/perf_socket_compat.hpp`로 이동
- single/multi에서 중복 정의 삭제
- metric header magic 값에 주석 추가:

```cpp
// single metric header: "SPF1" (Single Perf Format 1)
static constexpr uint32_t SINGLE_MAGIC = 0x53504631;
// multi metric header: "MPF1" (Multi Perf Format 1)
static constexpr uint32_t MULTI_MAGIC  = 0x4D504631;
```

완료 기준:
- guard 타입 정의 1곳
- magic 값 의도 문서화
- build + smoke perf 정상

### 단계 6. callback hot path 개선

할 일:
- single callback receiver의 `_queue_mutex` 경합 분석
- lock-free bounded SPSC queue 또는 atomic index 기반으로 교체 검토
- smoke perf: 정상 종료 + RESULT line 출력 확인

```cpp
// 현재: mutex 기반
std::lock_guard<std::mutex> lock(_queue_mutex);
_queue.push_back(event);

// 목표: lock-free SPSC queue 또는 atomic index
_queue[_write_idx.fetch_add(1, std::memory_order_relaxed)] = event;
```

- 변경 후 latency p95/p99이 기존과 동일한지 확인

완료 기준:
- callback hot path에 mutex 0건
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 7. 검증

할 일:
- full build 확인:
  ```bash
  cmake --build core/build --target cpp_perf_pair cpp_perf_pubsub \
    cpp_perf_dealer_dealer cpp_perf_dealer_router cpp_perf_router_router \
    cpp_perf_spot cpp_comp_src_dealer_dealer_server cpp_comp_src_dealer_dealer_client \
    cpp_comp_src_dealer_router_server cpp_comp_src_dealer_router_client \
    cpp_comp_src_router_router_server cpp_comp_src_router_router_client \
    cpp_comp_src_pubsub_server cpp_comp_src_pubsub_client \
    cpp_comp_src_spot_server cpp_comp_src_spot_client cpp_comp_src_stream_server
  ```
- single 전 패턴 smoke
- multi recv 전 패턴 smoke
- multi callback (STREAM, SPOT) smoke
- `core/perf/baseline` 대비 full comparable run
- 결과 파일 `bindings/cpp/perf/results/` 저장 확인
- direct native API grep 0건 유지 확인
- `doc/perf/PERF_POLICY.md` 준수 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지
- baseline 대비 throughput/latency regression 없음

## 6. 완료 정의

- TLS 해석이 `common/` 1곳으로 통합됨
- latency sampler가 `common/` 1곳으로 통합됨
- monitor wait가 `common/` 1곳으로 통합되고 시맨틱 차이가 파라미터로 명시됨
- socket/context guard가 `common/` 1곳에서 정의됨
- callback hot path에 mutex 0건
- metric header magic 의도가 문서화됨
- direct native API 호출 0건 유지
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약 준수
- `core/perf`와 동일한 측정 의미 유지
- 전체 패턴/전체 사이즈 정상 동작
- baseline 대비 regression 없음
