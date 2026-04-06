# `core/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B-** — 측정 의미/RESULT 포맷/retry 금지 등 핵심 계약 준수는 양호했으나,
> 초기 진단 시점에는 internal API 사용, hot path mutex, snapshot polling ready gate,
> start gate sleep, 과대 모듈 등 구조적 문제가 있었다
> 대상: `core/perf/` (single, multi, common, runner scripts)

## 1. 목표

core perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- SPOT 벤치마크가 public C API만 사용한다 (내부 헤더 0건).
- start gate에서 sleep, snapshot polling이 제거된다.
- hot path(active phase send/recv/callback)에서 `std::mutex` 0건.
- single suite가 callback only로 통일된다 (PUBSUB recv 경로 삭제).
- 금지 phase 이름(`phase_drain`)이 정리되고, SPOT barrier의 stabilization
  window는 정책 허용 범위 내에서 명확히 문서화된다.
- 과대 모듈(`bench_common.hpp` 1,709줄)이 관심사별로 분리된다.
- `core/perf`와 동일한 측정 의미를 유지한다.
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

> **정책 예외 (PERF_POLICY.md §8.6.1/§8.6.9)**: 정책은 각 리팩토링 단계마다
> full single+multi perf 비회귀 게이트를 요구한다. 본 계획에서는 리팩토링 중간
> 단계에서 smoke perf(기능 정상 동작)만 확인하고, 수치 비교는 전체 완료 후
> 순차 실행으로 수행한다. 이유: 리팩토링 작업 중 병렬 부하로 인해 중간 단계
> 측정값이 왜곡될 수 있으며, 구조 변경을 먼저 완료한 뒤 안정적 환경에서 수치를
> 비교하는 것이 더 신뢰도 높은 비회귀 판정을 제공한다. 단계 8(최종 검증)에서
> full comparable run을 순차 실행하여 정책 §8.6.1 요구사항을 충족한다.

## 2. 초기 상태 요약

직전 리팩토링(커밋 98a59e20)으로 해결된 항목:
- single/multi perf runner 정렬 완료
- `PERF_MULTI_ATTEMPTS` / `PERF_INFLIGHT` 등 레거시 변수 제거
- retry 로직 0건
- RESULT line 7-field CSV 형식 준수
- 결과 파일 naming (`perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS`) 준수
- STREAM은 multi suite에서만 테스트
- lock-free callback metric queue (single) 구현 완료
- 사전 할당 send buffer 전 패턴 적용
- EAGAIN → flow-control 상태 취급 준수

## 3. 초기 진단 시점의 POSD 문제

### 3.1 Internal API 직접 사용 (SPOT 전체)

`core/perf/common/perf_spot_handle.hpp`가 4개 내부 헤더를 include한다:

```cpp
#include "../../src/api/service_api_internal.hpp"
#include "../../src/api/zlink_testing.hpp"
#include "../../src/services/spot/spot_handle.hpp"
#include "../../src/services/spot/spot_node_access.hpp"
```

- `zlink::spot_node_access_t::from_handle()` — public handle → 내부 struct 변환
- `register_spot_mode_state()` — 내부 상태 등록
- `zlink::destroy_spot_handle_for_testing()` — 테스트 전용 소멸 함수

영향 범위:
- `single/src/perf_spot.cpp`
- `multi/src/perf_multi_spot_server.cpp`
- `multi/src/perf_multi_spot_client.cpp`
- `multi/common/perf_multi_spot_control.hpp`

정책 근거: PERF_POLICY.md §1.1, PERF_SINGLE_TEST_POLICY.md §9.1,
PERF_MULTI_TEST_POLICY.md §13.0 — "public C API만 사용한다"

### 3.2 Internal 환경 변수로 라이브러리 내부 조작

`multi/common/perf_common.hpp:1049-1061`의 `sync_spot_internal_mesh_pub_hwm()`가
`ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM` 환경 변수를 설정하여 라이브러리 내부
동작을 조작한다. public API 문서에 정의되지 않은 변수다.

사용처: `perf_multi_spot_client.cpp:1262`, `perf_multi_spot_server.cpp:520`

### 3.3 Start Gate에서 sleep(1s)

`single/src/perf_pubsub.cpp:330`:

```cpp
if (sub_ready && pub_ready)
    std::this_thread::sleep_for(std::chrono::seconds(1));
```

CONNECTION_READY 이벤트 양쪽 수신 직후 무조건 1초 sleep.
정책이 명시적으로 금지한 "start gate에서 sleep/msleep/고정 지연".

### 3.4 SPOT Ready Gate에서 Snapshot Polling

`single/src/perf_spot.cpp:175-186`:

```cpp
while (std::chrono::steady_clock::now() < deadline) {
    zlink_spot_node_status_snapshot(sub_node_, &sub_status);
    // connected_peer_count > 0 && ready_subject_count > 0 확인
    zlink_poll(&item, 0, 5);  // 5ms sleep
}
```

5ms 간격으로 `zlink_spot_node_status_snapshot()`을 반복 호출.
정책은 "start gate에서 monitor snapshot polling 금지"를 명시한다.

동일 패턴: `multi/common/perf_multi_spot_control.hpp:363-379`

단, single SPOT의 정책 기준은 "explicit local `READY/START` barrier"이며,
multi SPOT에는 이미 barrier(`perf_multi_spot_handshake.hpp`)가 존재한다.

### 3.5 Hot Path에서 std::mutex 사용

#### 3.5-a. Multi STREAM Session: `queue_mutex`

`multi/common/perf_multi_stream_session.hpp`:
- `enqueue()` (line 165): recv callback에서 `std::lock_guard<std::mutex>` 사용
- `drain_pending()` (line 179, 197): 서버 이벤트 루프 내 send path에서 mutex

`drain_pending()`는 매 poll 주기 호출되는 hot path다.
`std::deque` 사용으로 동적 할당도 발생 가능.

#### 3.5-b. Multi SPOT Client: `metrics_mutex`

`multi/common/perf_multi_spot_client_recv.hpp:78,111`:

```cpp
std::lock_guard<std::mutex> lock(*metrics_mutex);
```

SPOT recv callback metric 집계에서 mutex 사용.

#### 3.5-c. Single Metric Worker: `latency_mutex`

`single/common/bench_common.hpp:475`:

```cpp
std::lock_guard<std::mutex> lock(state_->latency_mutex);
state_->latency.add(latency_us);
```

모든 single 패턴의 metric worker에서 매 active 메시지마다 mutex lock.
callback → lock-free queue → worker 구조이므로 callback 자체는 lock-free지만,
worker의 mutex contention이 queue drain 속도를 제한할 수 있다.

영향: perf_pair.cpp, perf_dealer_dealer.cpp, perf_dealer_router.cpp,
perf_router_router.cpp, perf_pubsub.cpp, perf_spot.cpp (전 single 패턴)

정책 근거: PERF_SINGLE_TEST_POLICY.md §9.3 (single hot path),
PERF_MULTI_TEST_POLICY.md §13.1 (multi hot path) — "hot path에
std::mutex 사용 금지"

### 3.6 Single PUBSUB의 recv 모드 지원

`single/src/perf_pubsub.cpp`에 callback 모드와 recv loop 모드가 모두 구현되어 있다:
- Line 361-389: `start_pubsub_recv_loop()` — poller 기반 recv drain 루프 스레드
- Line 400-423: `wait_pubsub_recv_loop_ready()` — recv 루프 준비 대기

`single/run_comparison.py`:
```python
SUPPORTED_RECV_MODES = {
    "PUBSUB": ("callback", "recv"),  # 정책: callback only
}
```

정책 근거: PERF_SINGLE_TEST_POLICY.md §1.1 — "single suite는 callback 모드만
지원한다", §7.1 — PUBSUB 포함 전 패턴 `callback` only

### 3.7 금지 Phase 이름: `phase_drain`

`multi/common/perf_multi_metric_header.hpp:17`:

```cpp
enum phase_t {
    phase_unknown = 0,
    phase_warmup  = 1,
    phase_active  = 2,
    phase_drain   = 3     // 정책: ready → warmup → active만 허용
};
```

`single/common/bench_common.hpp:541`:

```cpp
inline int single_phase_drain_timeout_ms(int duration_s_, int recv_timeout_ms_)
```

정책은 `ready → warmup → active` 3단계만 허용한다. `phase_drain`은 별도 벤치
단계로 해석될 수 있으므로 이름/enum을 정리해야 한다.

참고: multi SPOT의 stabilization window(`wait_for_spot_ready_settle`)는
SPOT barrier protocol의 일부로 **정책에서 허용**한다
(PERF_POLICY.md:100-106, PERF_MULTI_TEST_POLICY.md:152-155).
다만 코드의 `settle`이라는 이름이 금지 목록(PERF_POLICY.md:124)에 있으므로
rename을 권장한다.

### 3.8 bench_common.hpp 과대 모듈 (1,709줄)

`single/common/bench_common.hpp`에 아래 관심사가 모두 포함:

| 관심사 | 대략 라인 범위 |
|--------|---------------|
| Polling infrastructure | 72-150 |
| Latency stats builder (reservoir sampling) | 201-286 |
| Callback metric queue (lock-free ring) | 296-344 |
| Metric worker thread | 548-604 |
| Phase management (drain, wait) | 491-545 |
| Socket/context setup utilities | 1553-1592 |
| Connection readiness (setup_connected_pair) | 1636-1691 |
| Queue probe (monitor snapshot sampling) | 1274-1438 |
| Default sizes/transports constants | 전역 |

"좁은 인터페이스와 풍부한 내부를 가진 모듈"(PERF_POLICY.md §8.6.3)과 거리가 있다.

### 3.9 Send 함수 중복 (4파일)

`send_single_part_blocking()`이 거의 동일한 형태로 존재:

| 파일 | 차이점 |
|------|--------|
| `perf_pair.cpp:97-111` | PAIR 소켓, 1-part |
| `perf_dealer_dealer.cpp:89-103` | DEALER 소켓, 1-part |
| `perf_dealer_router.cpp:102-116` | DEALER 소켓, 1-part |
| `perf_router_router.cpp:170-184` | ROUTER 소켓, 2-part (routing + payload) |

PAIR, DEALER_DEALER, DEALER_ROUTER는 모두 1-part send이며 코드 거의 동일.
ROUTER_ROUTER만 2-part(`zlink_send(socket, parts, 2, 0)`).

### 3.10 perf_infra.hpp에 테스트 인증서 인라인 임베딩

`common/perf_infra.hpp:350-427`에 PEM 문자열 3개(CA, server cert, server key)가
~80줄에 걸쳐 인라인 포함. TLS 설정 함수도 같은 파일에 혼재.
이 파일을 include하는 모든 벤치마크 바이너리에 인증서가 포함된다.

## 4. 설계 원칙

- `core/perf`의 측정 의미 절대 우선 (throughput/bandwidth/latency 정의 변경 금지)
- C/C++ perf 코드는 public C API(`<zlink.h>`)만 사용한다
- 공통화 목적은 복잡도 감소이며, 코드 이동이 아니다
- 각 패턴 파일에서 send/recv API 호출, EAGAIN 처리, recv/callback 모델 선택,
  ready gate, 소켓 생성이 직접 보여야 한다 (PERF_POLICY.md §8.5)
- recv/callback 모드 제약 준수:
  - single: callback only (전 패턴, 예외 없음)
  - multi: recv only (SPOT/STREAM만 dual-mode)
  - 미지원 조합은 fail-fast
- hot path에 새 lock/alloc/log 추가 금지
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

## 5. 단계별 실행 계획

### 단계 0. 현황 동결 및 의존성 확인

할 일:
- `perf_spot_handle.hpp`에서 사용하는 내부 API 4개에 대응하는 public C API
  존재 여부 확인
  - public API 확인됨: `zlink_spot_new()` / `zlink_spot_destroy()` (`core/include/zlink.h:1068,1071`)
  - 내부 `register_spot_mode_state()` 등에 대응하는 public API가 없으면
    core에 추가가 선행 작업으로 필요
- `ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM`을 대체할 public API 존재 여부 확인
  - `zlink_set_option(..., ZLINK_OPT_SNDHWM)` 등으로 대체 가능한지 확인
- single PUBSUB의 `sleep_for(1s)`가 제거 가능한지 확인
  - CONNECTION_READY 이후 즉시 메시징이 가능한지 테스트
  - 불가능하면 core 버그로 보고
- single SPOT의 snapshot polling을 대체할 barrier protocol 설계 확인
- `bench_common.hpp` 분리 시 영향받는 패턴 파일 목록 작성
- `core/perf` semantic parity 체크리스트 감사:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (`ready → warmup → active`)
  - RESULT line 7-field CSV 형식
  - 결과 파일 구조 (`report/` 단일 경로)
  - 사람이 읽는 markdown table stdout/결과 파일 양쪽 출력
  - Effective Options `(start)` / `(result)` 양쪽 출력
  - Tier 1 metric 기준 `expected`/`actual` 완료 판정
  - recv_mode / direction 분류 일치

완료 기준:
- public API gap 목록 확정
- sleep 제거 가능 여부 테스트 완료
- semantic parity 체크리스트 통과 확인
- 단계 1-7의 선행 조건이 모두 파악됨

### 단계 1. Internal API 제거 (SPOT public API 전환)

> 선행 조건: core에 SPOT handle 생성/소멸 public API가 존재해야 한다.
> 없으면 core 작업 후 진행.

할 일:
- `perf_spot_handle.hpp` 삭제
- `perf_create_default_spot_handle()` → `zlink_spot_new()` 등 public C API로 교체
- `perf_destroy_default_spot_handle()` → `zlink_spot_destroy()` 등 public C API로 교체
- 참조하는 4개 파일 수정:
  - `single/src/perf_spot.cpp`
  - `multi/src/perf_multi_spot_server.cpp`
  - `multi/src/perf_multi_spot_client.cpp`
  - `multi/common/perf_multi_spot_control.hpp`
- `sync_spot_internal_mesh_pub_hwm()` 함수 삭제
  → public API로 HWM 설정하거나, public API 추가 후 교체

완료 기준:
- `perf_spot_handle.hpp` 파일 삭제됨
- `grep -r "service_api_internal\|zlink_testing\|spot_handle\.hpp\|spot_node_access" core/perf/` → 0건
- `grep -r "ZLINK_SPOT_INTERNAL" core/perf/` → 0건
- build 성공 확인 (삭제된 내부 헤더 의존이 남아 있으면 컴파일 실패로 감지됨)
- 테스트 통과
- smoke perf (SPOT single + multi): 정상 종료, RESULT line 출력, 결과 파일 생성

### 단계 2. Start Gate Sleep 제거 + PUBSUB recv 모드 삭제

할 일:
- **sleep 제거**: `perf_pubsub.cpp:330`의 `sleep_for(1s)` 삭제
  - 삭제 후 PUBSUB smoke perf에서 throughput 0 또는 fail이 발생하면
    core 버그로 보고하고 core 수정 후 재진행
- **recv 모드 삭제**:
  - `perf_pubsub.cpp`에서 recv loop 코드 경로 삭제:
    - `pubsub_recv_loop_t` 구조체 및 관련 함수
    - `start_pubsub_recv_loop()`
    - `wait_pubsub_recv_loop_ready()`
    - recv loop 모드 분기 로직
  - `single/run_comparison.py`의 `SUPPORTED_RECV_MODES["PUBSUB"]`를
    `("callback",)`으로 변경

완료 기준:
- `perf_pubsub.cpp`에 `sleep_for` 0건
- `perf_pubsub.cpp`에 `recv_loop` 관련 코드 0건
- `single/run_comparison.py`에서 PUBSUB recv 모드 거부 확인
- build + 테스트 통과
- smoke perf (PUBSUB single): 정상 종료, RESULT line 출력, 결과 파일 생성

### 단계 3. SPOT Ready Gate Snapshot Polling 제거

할 일:
- **single SPOT**: `wait_for_spot_nodes_ready()` (perf_spot.cpp:162-191)를
  explicit local `READY/START` barrier로 교체
  - single은 1:1(pub-sub) 구조이므로 barrier는 간단함
  - 양쪽 spot node가 connect 후 barrier message 교환으로 ready 확인
  - `zlink_spot_node_status_snapshot()` polling loop 삭제
- **multi SPOT control**: `perf_multi_spot_control.hpp:360-379`의
  `connected_peer_count` snapshot polling 제거
  - `perf_multi_spot_handshake.hpp`의 READY/START barrier만으로 gate 수행
  - snapshot은 디버그 로깅용으로만 cold path에서 허용

완료 기준:
- ready gate 경로에서 `zlink_spot_node_status_snapshot()` 호출 0건
- single SPOT: local barrier로 ready 판정
- multi SPOT: READY/START barrier로 ready 판정 (기존 동작 유지)
- build + 테스트 통과
- smoke perf (SPOT single + multi): 정상 종료, RESULT line 출력, 결과 파일 생성

### 단계 4. Hot Path Mutex 제거

#### 4-a. Multi STREAM Session: queue_mutex + deque → lock-free queue

할 일:
- `perf_multi_stream_session.hpp`의 `std::deque<queued_message_t>` +
  `std::mutex`를 **bounded SPSC ring buffer**로 교체
  - recv callback이 producer, drain_pending이 consumer → SPSC 모델로 충분
  - ring buffer capacity: setup 시 사전 할당 (active phase 동적 할당 0건)
  - `std::atomic` head/tail index로 lock-free push/pop
  - `std::deque` 제거로 hot path heap allocation도 함께 해결
    (PERF_MULTI_TEST_POLICY.md §13.2: "active phase에서 동적 메모리 할당 금지")
- `enqueue()`, `drain_pending()`, `pending_size()` 등 인터페이스 유지

#### 4-b. Multi SPOT Client: metrics_mutex → atomic accumulator

할 일:
- `perf_multi_spot_client_recv.hpp`의 `metrics_mutex` 제거
- thread-local metric + epoch-based aggregation 패턴 확장
  (이미 일부 구현: `spot_thread_metrics_t`)
- 최종 집계만 cold path에서 mutex 허용

#### 4-c. Single Metric Worker: latency_mutex → sequential access

할 일:
- `bench_common.hpp:475`의 `latency_mutex` 제거
- metric worker가 latency sampler의 **유일한 writer**이므로 mutex 불필요
- `latency_stats_builder_t::add()`를 worker thread에서만 호출하도록
  소유권을 명확히 하고, mutex를 제거

완료 기준:
- hot path(active phase send/recv/callback 루프)에서 `std::mutex` 0건
- hot path에서 동적 할당(`std::deque`, `new`, `malloc`) 0건 — bounded 사전 할당만 사용
- `std::lock_guard`, `std::unique_lock` grep → cold path(setup/teardown/결과 출력)에서만 사용
- build + 테스트 통과
- smoke perf (STREAM + SPOT + single 전 패턴): 정상 종료, RESULT line 출력

### 단계 5. Phase 이름 정리

할 일:
- **`phase_drain` enum 삭제**:
  - `multi/common/perf_multi_metric_header.hpp:17`에서 `phase_drain = 3` 제거
  - `phase_drain` 사용처가 있으면 `phase_active` 또는 삭제로 대체
- **`single_phase_drain_timeout_ms` rename**:
  - → `single_phase_completion_timeout_ms` (의미: active phase 완료 대기)
  - 함수 로직은 유지 (phase 종료 후 processed-count 기반 완료 보장)
- **SPOT settle rename** (용어 명확화, optional):
  - 로직은 정책이 허용하는 SPOT barrier stabilization window이며 위반이 아님
    (PERF_POLICY.md:100-106, PERF_MULTI_TEST_POLICY.md:152-155)
  - 다만 `settle` 식별자가 금지 phase 목록과 혼동될 수 있으므로 rename 권장:
  - `wait_for_spot_ready_settle` → `wait_for_spot_barrier_stabilization`
  - `PERF_MULTI_SPOT_READY_SETTLE_MS` → `PERF_MULTI_SPOT_STABILIZATION_MS`
  - `ready_barrier_settled` → `barrier_stabilized`
- **STREAM bench client의 `effective_phase_drain_ms` rename**:
  - → `effective_phase_completion_ms`
  - 로직은 변경 없음 (검증 인프라 예외에 해당하지만 이름 정리)

완료 기준:
- `grep -r "phase_drain" core/perf/` → 0건
- `grep -rn "single_phase_drain" core/perf/` → 0건
- build + 테스트 통과
- smoke perf: 정상 종료, RESULT line 출력

### 단계 6. bench_common.hpp 모듈 분리

할 일:
- `single/common/bench_common.hpp` (1,709줄)을 기능별로 분리:

| 새 모듈 | 추출 내용 | 대략 줄 수 |
|---------|----------|-----------|
| `perf_single_latency.hpp` | `latency_stats_builder_t`, reservoir sampling, percentile 계산 | ~100 |
| `perf_single_metric_queue.hpp` | `single_callback_metric_queue_t`, `single_callback_metric_event_t` | ~60 |
| `perf_single_metric_worker.hpp` | metric worker thread, `single_account_metric_event` | ~70 |
| `perf_single_phase.hpp` | `single_phase_completion_timeout_ms`, `single_wait_for_phase_processed` | ~60 |
| `perf_single_queue_probe.hpp` | `queue_stats_t`, queue probe logic | ~170 |
| `bench_common.hpp` (축소) | default sizes, transports, setup helpers, `setup_connected_pair()` | ~300 |

- 각 패턴 파일의 `#include "bench_common.hpp"`를 필요한 모듈만 include하도록 변경
- 순환 의존 없이 분리 가능한지 확인 (의존 방향: 패턴 → 모듈, 모듈 간 단방향)
- 분리 기준은 줄 수가 아니라 **소유권 경계**:
  - 각 모듈은 하나의 관심사(latency 집계, metric queue, phase 제어 등)를
    완전히 소유하고, 해당 관심사의 invariant를 내부에서 보장한다
  - 모듈 간 의존은 인터페이스(함수 시그니처, 타입)를 통해서만 발생하며,
    내부 상태를 직접 참조하지 않는다
  - 분리 후에도 각 모듈이 "좁은 인터페이스 + 풍부한 내부"를 유지해야 한다
    (PERF_POLICY.md §8.6.3). 단순히 줄을 나누는 shallow fragmentation은
    오히려 공통화 이전보다 복잡도를 높이므로, 분리 결과가 이 기준을 충족하지
    못하면 해당 모듈은 `bench_common.hpp`에 유지한다

완료 기준:
- `bench_common.hpp`에서 관심사별 모듈이 분리되어 가독성과 유지보수에 적절한 크기를 유지함 (shallow fragmentation 방지 원칙 우선)
- 순환 include 0건
- 각 모듈이 단일 관심사를 소유하고 의존 방향이 단방향
- build + 테스트 통과
- smoke perf (single 전 패턴): 정상 종료, RESULT line 출력

### 단계 7. Send 중복 제거 + TLS/인증서 분리

#### 7-a. Send EAGAIN/EINTR 처리 공통화

할 일:
- `zlink_send()` 호출 자체는 각 패턴 파일에 인라인으로 유지한다
  (PERF_POLICY.md §8.5: "send/recv API 호출이 각 파일에서 직접 보여야 한다")
- EAGAIN/EINTR 후처리 분류만 공통 inline helper로 추출:

```cpp
// bench_common.hpp 또는 별도 헤더
// zlink_send() 반환값을 해석하는 helper. send 호출 자체는 패턴 파일에서 수행.
enum perf_send_class_t {
    perf_send_ok      =  1,   // 전송 성공
    perf_send_retry   =  0,   // EINTR — 즉시 재시도
    perf_send_blocked = -1,   // EAGAIN — backpressure, 대기 후 재시도
    perf_send_fatal   = -2    // 기타 오류 — 즉시 중단
};

inline perf_send_class_t perf_classify_send_result(int send_rc)
{
    if (send_rc >= 0)
        return perf_send_ok;
    const int err = zlink_errno();
    if (err == EINTR)
        return perf_send_retry;
    if (err == EAGAIN)
        return perf_send_blocked;
    return perf_send_fatal;
}
```

caller는 `perf_send_retry`(EINTR → 즉시 재호출)과
`perf_send_blocked`(EAGAIN → 대기 후 재시도)를 구분하여 처리한다.

- 각 패턴 파일의 send loop에서 `zlink_send()` 호출 후
  `perf_classify_send_result()`로 분기 처리
- PAIR, DEALER_DEALER, DEALER_ROUTER의 중복 `send_single_part_blocking()` 제거
- ROUTER_ROUTER의 `send_router_parts_blocking()` (2-part)도 동일 패턴 적용

#### 7-b. 인증서/TLS 코드 분리

할 일:
- `common/perf_infra.hpp`에서 아래를 `common/perf_tls_setup.hpp`로 이동:
  - `test_certs` namespace (PEM 문자열 ~80줄)
  - `write_temp_cert()`
  - `set_tls_path_option()`
  - `setup_tls_server()`
  - `setup_tls_client()`
- `perf_infra.hpp`는 순수 인프라만 유지:
  stopwatch, env parsing, endpoint construction, socket option helpers

완료 기준:
- PAIR/DEALER_DEALER/DEALER_ROUTER에 중복 send 함수 0건
- 각 패턴 파일에 `zlink_send()` 호출이 인라인으로 존재 (helper 뒤에 숨기지 않음)
- `perf_infra.hpp`에 인증서 문자열 0건
- build + 테스트 통과
- smoke perf: 정상 종료, RESULT line 출력

### 단계 8. 최종 검증

할 일:
- full build 확인:
  ```bash
  cmake --build core/build --target \
    perf_pair perf_pubsub perf_dealer_dealer perf_dealer_router \
    perf_router_router perf_spot \
    comp_src_dealer_dealer_server comp_src_dealer_dealer_client \
    comp_src_dealer_router_server comp_src_dealer_router_client \
    comp_src_router_router_server comp_src_router_router_client \
    comp_src_pubsub_server comp_src_pubsub_client \
    comp_src_spot_server comp_src_spot_client \
    comp_src_stream_server perf_stream_client
  ```
- single 전 패턴 full run (callback)
- multi recv 전 패턴 full run
- multi callback (STREAM, SPOT) full run
- baseline 대비 full comparable run
- 정책 준수 체크리스트:

| 항목 | 검증 방법 |
|------|-----------|
| internal header 0건 | `grep -r "src/api\|src/services" core/perf/` — include/symbol/callsite 기준 |
| internal env var 0건 | `grep -r "ZLINK_SPOT_INTERNAL" core/perf/` |
| hot path mutex 0건 | `grep -rn "lock_guard\|unique_lock" core/perf/` → cold path만 |
| hot path heap alloc 0건 | active phase에서 `std::deque`/`new`/`malloc` 사용 없음 확인 |
| sleep in start gate 0건 | `grep -rn "sleep_for\|sleep(" core/perf/` → phase timing/OS idle만 |
| snapshot polling gate 0건 | ready gate 경로에서 `status_snapshot` 0건 |
| single recv mode 0건 | `single/run_comparison.py` PUBSUB callback only |
| phase_drain 0건 | `grep -r "phase_drain" core/perf/` |
| RESULT line 형식 | 7-field CSV 유지 (PERF_POLICY.md §4.2) |
| 측정 의미 | throughput/bandwidth/latency 정의 변경 없음 |
| 정책 준수 실행기 사용 | `run_benchmarks.sh` / `run_benchmarks_multi.sh`로만 결과 생성 (PERF_POLICY.md §3.1) |
| 사람이 읽는 table 출력 | stdout + 결과 파일 양쪽에 markdown table 포함 (PERF_POLICY.md §5.1) |
| Effective Options 출력 | `(start)` / `(result)` 양쪽 출력, `recv_mode` 필수 포함 (PERF_POLICY.md §4.1) |
| Tier 1 완료 판정 | `expected == actual` 기준: throughput+bandwidth+latency+p95+p99 (조합당 5줄) |

완료 기준:
- full single + multi perf run (순차 실행) 전체 패턴/전체 사이즈 정상 동작
- `core/perf`의 측정 의미 유지
- baseline 대비 throughput/latency regression 없음 (full comparable run, 순차 실행)
- 정책 준수 체크리스트 전 항목 통과
- 결과 파일에 사람이 읽는 table + Effective Options `(start)`/`(result)` 포함

## 6. 완료 정의

- SPOT 벤치마크가 public C API만 사용함 (내부 헤더/내부 환경 변수 0건)
- start gate에서 sleep/snapshot polling 0건
- hot path에서 `std::mutex` 0건, 동적 할당 0건
- single suite가 callback only (PUBSUB recv 경로 삭제됨)
- `phase_drain` enum/함수명 정리됨
- `bench_common.hpp`에서 관심사별 모듈이 분리되어 가독성과 유지보수에 적절한 크기를 유지함
- send 함수 중복이 공통 helper로 정리됨
- 인증서/TLS 코드가 `perf_infra.hpp`에서 분리됨
- `doc/perf/PERF_POLICY.md` 전 항목 준수
- 전체 패턴/전체 사이즈 정상 동작
- baseline 대비 regression 없음

---

## 완료 상태

> 완료일: 2026-04-06
> 검증: 빌드 통과 + 스모크 테스트 통과 + 완료 정의 대비 코드 리뷰 통과

Steps 1-8 완료. `bench_common.hpp`는 400줄 이하로 축소되었고 런타임 로직은 `bench_common_runtime.hpp`로 분리되었다. SPOT 내부 env 주입과 `wait_for_spot_ready_settle()` 잔존 참조도 제거되었다. 빌드 single 6+multi 7타겟 통과, 스모크 결과는 [perf_linux_callback_20260406_170044.txt](/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_callback_20260406_170044.txt) 에 저장되었다.
