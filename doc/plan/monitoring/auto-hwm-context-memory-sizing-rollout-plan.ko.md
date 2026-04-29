# Auto-HWM Context Memory Sizing Rollout Plan

이 문서는 구현 순서와 검증 게이트를 정리한 **실행 계획 문서**다.
설계 자체는 아래 draft 문서를 기준으로 삼는다.

- [Auto-HWM 개선 정책 Draft](../../draft/auto-hwm-context-memory-sizing.ko.md)

이 plan의 목적은 "무엇을 만들 것인가"를 다시 설명하는 것이 아니라, 그 설계를
사용자 개입 없이 끝까지 반영하는 순서와 통과 조건을 고정하는 것이다.

핵심 원칙은 아래와 같다.

1. 먼저 현재 상태와 영향 범위를 확인한다.
2. 공개 계약 기준인 `core/include/zlink.h`, `core/include/zlink_enum.h`를 먼저
   반영한다.
3. core auto-HWM planner와 SpotNode 내부 적용을 구현한다.
4. draft 문서 기준으로 core 반영 여부를 반복 리뷰한다.
5. 그 다음 `core/src` 전체를 POSD 기준으로 반복 리팩토링한다.
6. core, C sample, C perf를 모두 검증한다.
7. 그 뒤에만 정식 문서와 bindings를 반영한다.
8. 각 binding native 동기화, binding 라이브러리 수정, binding 검증을 모두 끝낸다.

이 plan에서 "통과"는 한 번 실행했다는 뜻이 아니다. 실패한 항목이 있으면 원인을
고치고 같은 게이트를 다시 실행해서 성공한 상태를 뜻한다.

---

## 0. 무인 실행 규칙과 공통 명령

이 plan은 사람이 중간에 판단해 주는 것을 전제로 하지 않는다. 아래 규칙을 모든
단계에 적용한다.

### 0.1 무인 실행 규칙

1. 실패한 명령이 있으면 같은 단계 안에서 원인을 분석하고 수정한 뒤 다시 실행한다.
2. "대부분 통과"나 "일부 skip"은 통과로 보지 않는다.
3. sample 또는 perf 디렉터리가 실제로 없을 때만 `N/A`로 기록할 수 있다.
4. 디렉터리나 runner가 있는데 실패하면 반드시 수정한다.
5. perf smoke에서 실제 result row 없이 0-result를 성공으로 처리하면 실패로 본다.
6. `core/src` 또는 `core/include`를 바꾼 뒤에는 항상 `cmake --build core/build`를
   먼저 실행한다.
7. perf 판단은 항상 `core/build` runtime 기준으로 한다.
8. `bindings/c/perf` runner가 출력한 `Perf runtime libzlink:` 경로가
   `core/build` 아래가 아니면 실패로 본다.
9. public header를 바꾸면 binding 수정 전에 native header/library 동기화를 먼저
   수행한다.
10. 기존 사용자 변경은 되돌리지 않는다.

### 0.2 공통 core 명령

```bash
git status --short
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

core 선별 회귀 테스트는 아래 명령을 사용한다.

```bash
ctest --test-dir core/build --output-on-failure \
  -R "test_ctx_options|test_monitor_socket_contract|test_monitor_enhanced|test_monitor_perf_contract|test_hwm_pubsub|test_router_mandatory_hwm|test_backpressure_matrix|test_backpressure_oneway_matrix|test_spot_service_introspection|test_spot_runtime_activation|test_spot_pubsub_scenario|test_spot_dispatch_event|test_spot_poller|unittest_ctx_runtime|unittest_socket_runtime|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|unittest_spot_subject_access"
```

### 0.3 C sample/perf 공통 명령

```bash
bindings/c/samples/run_samples.sh

ctest --test-dir bindings/c/build --output-on-failure

PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks.sh \
  --pattern ALL \
  --runs 1 \
  --duration 1 \
  --msg-sizes 64 \
  --transports tcp \
  --reuse-build \
  --results-tag auto_hwm_v2_single_smoke

PERF_DISABLE_RESOURCE_METRICS=1 \
PERF_MULTI_RUN_COOLDOWN_MS=0 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern ALL \
  --runs 1 \
  --duration 1 \
  --clients 2 \
  --msg-sizes 64 \
  --transports tcp \
  --connect-concurrency 2 \
  --transport-transition-ms 0 \
  --pattern-transition-ms 0 \
  --server-ready-timeout-ms 10000 \
  --connect-ready-timeout-ms 5000 \
  --server-shutdown-timeout-ms 5000 \
  --reuse-build \
  --results-tag auto_hwm_v2_multi_smoke
```

### 0.4 C perf targeted 명령

auto-HWM v2 구현과 함께 `bindings/c/perf` single/multi runner에는
`--auto-hwm-profile low_latency|balanced|throughput` 옵션을 추가한다. 이 옵션은
벤치마크 대상 context에 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`을 설정해야 한다.

```bash
PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern PUBSUB \
  --runs 1 \
  --duration 2 \
  --clients 100 \
  --msg-sizes 262144 \
  --transports tcp \
  --auto-hwm-profile balanced \
  --reuse-build \
  --results-tag auto_hwm_v2_pubsub_256k_balanced

PERF_DISABLE_RESOURCE_METRICS=1 \
PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US=5000 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT \
  --runs 1 \
  --duration 2 \
  --clients 100 \
  --msg-sizes 262144 \
  --transports tcp \
  --auto-hwm-profile balanced \
  --reuse-build \
  --results-tag auto_hwm_v2_spot_256k_balanced

PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern DEALER_DEALER \
  --runs 1 \
  --duration 2 \
  --clients 100 \
  --msg-sizes 65536 \
  --transports tcp \
  --auto-hwm-profile balanced \
  --reuse-build \
  --results-tag auto_hwm_v2_dealer_dealer_64k_balanced
```

STREAM scale smoke는 아래 세 명령을 모두 실행한다.

```bash
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM --runs 1 --duration 1 --clients 1000 --msg-sizes 65536 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_v2_stream_1000
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM --runs 1 --duration 1 --clients 5000 --msg-sizes 65536 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_v2_stream_5000
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM --runs 1 --duration 1 --clients 10000 --msg-sizes 65536 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_v2_stream_10000
```

SPOT fanout 500 smoke는 아래 명령을 사용한다.

```bash
PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT \
  --runs 1 \
  --duration 1 \
  --clients 500 \
  --msg-sizes 64,1024,65536 \
  --transports tcp \
  --auto-hwm-profile balanced \
  --reuse-build \
  --results-tag auto_hwm_v2_spot_fanout_500
```

### 0.5 binding 검증 명령 목록

각 binding은 아래 네 종류의 명령을 모두 실행한다. 명령이 실패하면 같은 binding
단계에서 원인을 고치고 다시 실행한다.

#### C

```bash
ctest --test-dir bindings/c/build --output-on-failure
bindings/c/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --reuse-build --results-tag auto_hwm_v2_c_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --reuse-build --results-tag auto_hwm_v2_c_multi
```

#### C++

```bash
bindings/cpp/tests/run_tests.sh
bindings/cpp/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/cpp/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_cpp_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/cpp/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_cpp_multi
```

#### Go

```bash
bindings/go/tests/run_tests.sh
bindings/go/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/go/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_go_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/go/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_go_multi
```

#### Python

```bash
bindings/python/tests/run_tests.sh
bindings/python/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_python_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_python_multi
```

#### Rust

```bash
bindings/rust/tests/run_tests.sh
bindings/rust/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_rust_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_rust_multi
```

#### Node

```bash
npm --prefix bindings/node test
npm --prefix bindings/node run samples
PERF_DISABLE_RESOURCE_METRICS=1 bindings/node/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_node_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/node/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_node_multi
```

#### Java

```bash
bindings/java/tests/run_tests.sh
bindings/java/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/java/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_java_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/java/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_java_multi
```

#### Dotnet

```bash
bindings/dotnet/tests/run_tests.sh
bindings/dotnet/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/dotnet/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_dotnet_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/dotnet/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_v2_dotnet_multi
```

### 0.6 정식 문서 대상 파일

정식 문서 반영 게이트에서는 최소 아래 파일을 확인하고 필요한 내용을 반영한다.

- `doc/internals/socket-option-defaults.ko.md`
- `doc/internals/socket-option-defaults.md`
- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/spec/core/monitoring.ko.md`
- `doc/spec/core/monitoring.md`
- `doc/spec/core/socket/pub.ko.md`
- `doc/spec/core/socket/pub.md`
- `doc/spec/core/socket/sub.ko.md`
- `doc/spec/core/socket/sub.md`
- `doc/spec/core/socket/dealer.ko.md`
- `doc/spec/core/socket/dealer.md`
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/stream.ko.md`
- `doc/spec/core/socket/stream.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/guide/06-monitoring.ko.md`
- `doc/guide/06-monitoring.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/10-performance.ko.md`
- `doc/guide/10-performance.md`
- `doc/guide/12-socket-options.ko.md`
- `doc/guide/12-socket-options.md`
- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`

각 파일을 수정하지 않기로 판단한 경우에도 진행 로그에 "검토 후 변경 없음"으로
기록한다.

---

## 1. 시작 상태 확인 게이트

구현 전에 현재 repository 상태와 영향 범위를 먼저 확인한다. 기존 사용자 변경은
되돌리지 않는다.

### 1.1 실행 명령

- `git status --short`
- `rg -n "AUTO_HWM|auto_hwm|HWM|SPOT_BOOTSTRAP|MSG_UNIT|monitor_snapshot" core/include core/src core/tests bindings doc`
- `rg -n "ZLINK_CTX_OPT_AUTO_HWM|zlink_ctx_set|zlink_ctx_get|zlink_monitor_snapshot_t" core/include core/src bindings`

### 1.2 확인 항목

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- `core/src/core/auto_hwm_policy.*`
- `core/src/core/ctx.*`
- `core/src/sockets/socket_base.*`
- `core/src/services/spot/*auto*hwm*`
- `core/src/services/spot/*node*`
- monitor snapshot 생성 경로
- C binding native header/runtime 동기화 경로
- 각 언어 binding의 context option, enum, monitor snapshot wrapper
- `bindings/c/perf` runner의 runtime 선택 경로

### 1.3 진행 기록

각 반복에서 아래를 기록한다.

- 수정 파일
- 실행 명령
- 실패 원인
- 해결 내용

---

## 2. 공개 계약 반영 게이트

공개 계약 기준은 항상 `core/include/zlink.h`와 `core/include/zlink_enum.h`다.
draft에 있는 API와 enum 변경은 먼저 공개 헤더에 반영한다.

### 2.1 반영 항목

- `zlink_auto_hwm_profile_t` 추가
- `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY`
- `ZLINK_AUTO_HWM_PROFILE_BALANCED`
- `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT`
- `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`
- `ZLINK_CTX_AUTO_HWM_PROFILE_DFLT`
- `zlink_ctx_set()` / `zlink_ctx_get()`의 profile option 처리
- 알 수 없는 profile 값의 `EINVAL` 실패 경로
- `zlink_monitor_snapshot_t`의 auto-HWM v2 디버깅 필드

### 2.2 추가하지 않는 항목

아래는 draft 기준으로 첫 구현 범위에 추가하지 않는다.

- `ZLINK_CTX_OPT_AUTO_HWM_PUBLISH_FANOUT`
- 새 SpotNode HWM option
- 새 socket HWM option

SpotNode publish fanout limit은 기존
`ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP` 값을 재사용한다.

### 2.3 검증

- `cmake --build core/build`
- profile option set/get 단위 테스트
- 기본값 조회 테스트
- 잘못된 profile 값의 `EINVAL` 테스트
- monitor snapshot struct 크기와 필드 채움 테스트

`core/include`를 바꾼 뒤에는 반드시 `cmake --build core/build`로 runtime을 다시
만든다.

---

## 3. core auto-HWM planner 구현 게이트

이 단계에서는 기존 auto-HWM 계산을 draft의 v2 계산식으로 대체한다. context
memory는 HWM을 크게 만드는 값이 아니라 전체 queue memory 상한으로만 사용한다.

### 3.1 반영 항목

- 내부 `auto_hwm_policy_class` 추가
- public socket type에서 policy class로 변환하는 함수 추가
- profile별 unit budget table 추가
- profile별 `MsgUnit(B)` size cap table 추가
- `fanout`, `spot_data`, `routed`, `peer_queue`, `stream`, `recv_ingress`,
  `control` class 반영
- `DEALER`는 첫 구현에서 `peer_queue` cap 적용
- `STREAM`은 `stream` class budget과 cap 적용
- floor는 모든 class에서 1로 정리
- 최종 상한은 별도 hard cap이 아니라 `size_cap`으로 정리
- context 전체 auto-HWM 대상 소켓 기준 fair share 계산
- 단일 socket refresh가 context 전체 재계산을 우회하지 않도록 정리

### 3.2 계산식 기준

```text
context_budget = user configured context memory budget
queue_budget = context_budget - reserve_budget

socket_class = auto_hwm_policy_class(socket)
socket_unit_budget = profile_unit_budget(socket_class)
socket_count = max(1, class_count(socket))

socket_budget_cap = socket_unit_budget * socket_count
socket_budget = min(socket_budget_cap, fair_share(queue_budget, socket))

per_connection_budget = socket_budget / socket_count
memory_hwm = per_connection_budget / MsgUnit(B)
size_cap = profile_size_class_cap(profile, socket_class, MsgUnit(B))

effective_hwm =
  clamp(min(memory_hwm, size_cap), 1, size_cap)
```

### 3.3 검증

- `cmake --build core/build`
- planner 단위 테스트
- profile별 unit budget 선택 테스트
- policy class mapping 테스트
- `MsgUnit(B)` 구간별 `size_cap` 테스트
- context budget이 작을 때 HWM이 낮아지는 테스트
- context budget이 커도 `size_cap`을 넘지 않는 테스트
- 수동 `SNDHWM` 또는 `RCVHWM`이 auto-HWM에 덮이지 않는 테스트
- deferred HWM 적용 테스트

---

## 4. SpotNode auto-HWM 적용 게이트

SpotNode는 외부 spot handle만 계산하면 안 된다. SpotNode 내부 소켓도 같은 planner
정책으로 계산해야 한다.

### 4.1 반영 항목

- `local-pub`은 `spot_data` class로 계산
- `mesh-pub`은 `spot_data` class로 계산
- `ingress-sub`은 `recv_ingress` class로 계산
- `mesh-xsub`은 `recv_ingress` class로 계산
- `internal-router`는 `routed` class로 계산
- `external-router`는 `routed` class로 계산
- `peer_ctrl_pub/sub`은 `control` class로 계산
- local fanout과 mesh fanout을 같은 숫자로 뭉개지 않고 분리
- 자동 계산값은 public setter를 우회하는 내부 apply 경로로 반영
- 자동 계산값 적용이 수동 override 플래그를 켜지 않도록 보장

### 4.2 effective publish fanout

첫 구현에서는 아래 계산식을 사용한다.

```text
publish_fanout_limit =
  max(1, ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP)

candidate_publish_targets =
  max(local_sub_spot_count, active_peer_count, observed_scope_count)

effective_publish_fanout =
  max(1, min(candidate_publish_targets, publish_fanout_limit))
```

`total_spot_count`는 fanout queue budget이 아니라 metadata budget에만 반영한다.

### 4.3 검증

- `cmake --build core/build`
- SpotNode 내부 socket class mapping 테스트
- effective publish fanout 계산 테스트
- total spot count가 fanout queue budget으로 들어가지 않는 테스트
- `ZLINK_SPOT_NODE_OPT_PUB_HWM`
- `ZLINK_SPOT_NODE_OPT_SUB_HWM`
- `ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM`
- `ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM`
- 위 수동 override 우선순위 테스트
- SpotNode monitor snapshot에서 profile, policy class, unit budget, size cap,
  effective publish fanout 확인 테스트

---

## 5. core 변경 뒤 draft 적용 리뷰 게이트

core 변경 뒤에는 바로 bindings나 정식 문서로 넘어가지 않는다. draft 문서의 각
항목이 실제 core 코드와 공개 헤더에 반영되었는지 하나씩 대조한다.

### 5.1 리뷰 체크 항목

- context memory 산정 공식
- profile enum과 기본값
- 추가하지 않는 API 목록
- `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP` fanout limit 재사용
- profile별 unit budget
- policy class mapping
- fair share 계산
- floor 1과 `size_cap` 상한
- 단일 socket refresh의 context 전체 재계산 예약
- SpotNode effective publish fanout
- 수동 HWM override 우선순위
- large message SPOT 발행률 제한 해석
- monitor snapshot 필드
- 회귀 테스트 항목

### 5.2 반복 절차

1. `cmake --build core/build`
2. core 관련 테스트 실행
3. draft 각 섹션을 코드와 공개 헤더에 대조
4. 미적용 항목을 기록
5. 미적용 항목이 있으면 core 수정
6. 다시 build / test / review 반복
7. 미적용 항목이 `0`이 될 때만 다음 단계로 넘어간다

---

## 6. POSD 기반 리팩토링 게이트

draft 항목이 모두 core에 반영된 뒤에도 바로 다음 단계로 넘어가지 않는다. 그 다음
`core/src` 전체를 대상으로 POSD 기반 리팩토링 단계를 수행한다.

### 6.1 범위

- `core/src/api/`
- `core/src/core/`
- `core/src/sockets/`
- `core/src/services/`
- 그 밖의 `core/src` 전체

### 6.2 hotspot

- profile option 처리 지식이 context, API, planner에 중복되어 있지 않은가
- unit budget과 size cap table이 여러 파일에 흩어져 있지 않은가
- socket type에서 policy class로 바꾸는 지식이 한 곳에 있는가
- SpotNode 내부 socket class와 count 기준이 여러 경로에 중복되어 있지 않은가
- 수동 HWM override와 자동 apply 경로가 섞이지 않았는가
- monitor snapshot 채움 코드가 planner 내부 구조를 과하게 노출하지 않는가
- 단일 socket refresh와 context 전체 재계산이 서로 다른 정책을 쓰지 않는가

### 6.3 필수 검색 명령

POSD 리뷰는 아래 검색을 최소 기준으로 사용한다. 검색 결과가 나오면 관련 코드를
확인하고, 실제 위험 신호인지 판단해 진행 로그에 남긴다.

```bash
rg -n "auto_hwm.*profile|profile.*auto_hwm|AUTO_HWM_PROFILE" core/src core/include
rg -n "unit_budget|size_cap|policy_class|peer_queue|spot_data" core/src core/include
rg -n "refresh_auto_hwm_policy|auto_hwm_recalculate|deferred.*hwm|manual.*hwm" core/src
rg -n "return [A-Za-z0-9_:]+\\([^;]*\\);|TODO|FIXME|pass[-_ ]?through" core/src
rg -n "ZLINK_CTX_OPT_AUTO_HWM_PROFILE|zlink_auto_hwm_profile_t" core/src core/include bindings
```

아래 조건이 모두 만족되어야 POSD follow-up을 0개로 기록할 수 있다.

- profile table이 한 모듈에 모여 있다.
- socket type에서 policy class로 변환하는 지식이 한 곳에 있다.
- SpotNode 내부 socket별 count 기준이 한 곳에서 관리된다.
- manual override 적용 경로와 auto apply 경로가 분리되어 있다.
- context 전체 재계산 경로와 단일 socket refresh 경로가 같은 planner를 사용한다.
- monitor snapshot은 planner 결과를 읽기만 하고 계산식을 복제하지 않는다.

### 6.4 반복 절차

1. `core/src` 전체에서 POSD 위험 신호를 열거한다.
2. 각 위험 신호가 어떤 POSD 원칙에 어긋나는지 기록한다.
3. 수정 방향을 두 가지 이상 검토하고, 더 나은 쪽을 선택한다.
4. core 코드를 수정한다.
5. `cmake --build core/build`
6. core 테스트를 다시 실행한다.
7. 다시 `core/src` 전체를 리뷰한다.
8. 남은 POSD follow-up이 `0`이 될 때까지 반복한다.

---

## 7. core 전체 검증 게이트

core 구현과 POSD 리팩토링이 끝나면 전체 검증 게이트를 통과해야 한다.

### 7.1 실행 항목

- `cmake --build core/build`
- `ctest --test-dir core/build --output-on-failure`
- 0.2의 core 선별 회귀 테스트 명령
- auto-HWM profile set/get 테스트
- auto-HWM planner 테스트
- SpotNode auto-HWM 테스트
- monitor snapshot 테스트

### 7.2 통과 조건

- core 전체 테스트 성공
- draft 미적용 항목 0개
- POSD follow-up 0개
- `core/build` runtime이 최신 source보다 오래되지 않음
- 새 profile option, planner, SpotNode, monitor snapshot 테스트가 core 전체 테스트에
  포함되어 있음

---

## 8. C sample과 perf 검증 게이트

perf 판단은 항상 `core/build` runtime 기준으로 한다. `build_cpp_release`나 다른
임시 빌드 디렉터리 결과로 판단하지 않는다.

### 8.1 실행 항목

- 0.3의 C sample/perf 공통 명령 전체
- 0.4의 C perf targeted 명령 전체

### 8.2 필수 확인

- perf runner가 실행 전에 실제 `core/build/lib/libzlink.so` 경로를 출력한다.
- perf runner가 `core/build` runtime이 source보다 오래되면 실패한다.
- single smoke는 전체 패턴을 통과한다.
- multi smoke는 전체 패턴을 통과한다.
- SPOT 256 KiB latency 해석은 HWM cap과 publish interval을 함께 기록한다.
- targeted perf 결과 파일에 실제 result row가 있어야 한다.
- `--auto-hwm-profile balanced`가 실제 context option 설정으로 이어졌는지 snapshot이나
  perf detail로 확인한다.

---

## 9. 정식 문서 반영 게이트

core와 C sample/perf 검증을 통과한 뒤에만 정식 문서를 수정한다. 구현 전 draft
내용을 정식 spec, guide, internals에 섞지 않는다.

### 9.1 반영 대상

1. 0.6의 정식 문서 대상 파일 전체
2. 추가로 `rg -n "auto-HWM|AUTO_HWM|HWM|monitor snapshot|SPOT_BOOTSTRAP" doc`
   결과에서 새 정책과 충돌하는 문서

### 9.2 반영 항목

- auto-HWM v2의 context memory 의미
- profile enum과 context option
- profile별 unit budget
- `MsgUnit(B)`별 size cap
- SpotNode effective publish fanout
- 수동 HWM override 우선순위
- monitor snapshot 새 필드
- large message SPOT perf 해석
- `bindings/c/perf` runtime 기준

### 9.3 검증

```bash
rg -n "ZLINK_CTX_OPT_AUTO_HWM_PROFILE|zlink_auto_hwm_profile_t|auto_hwm_profile|auto_hwm_policy_class|auto_hwm_size_cap" doc/internals doc/spec doc/guide
rg -n "ZLINK_CTX_OPT_AUTO_HWM_PUBLISH_FANOUT" doc/internals doc/spec doc/guide
rg -n "build_cpp_release|0-result|zero result|가짜 perf" doc/internals doc/spec doc/guide bindings
```

두 번째 검색은 결과가 없어야 한다. 세 번째 검색은 새 정책과 충돌하는 오래된
설명이 있으면 실패로 보고 수정한다.

---

## 10. bindings native 동기화 게이트

정식 문서를 반영한 뒤에는 각 binding의 native 폴더를 최신 core 기준으로
동기화한다. bindings 라이브러리 수정 전에 먼저 수행한다.

### 10.1 순서

1. `core/include/` 공개 헤더 최신화 확인
2. `cmake --build core/build`
3. `core/build` runtime 최신화 확인
4. 각 binding native header/library 동기화

### 10.2 Linux x86_64 동기화 명령

Linux x86_64 native runtime이 있는 binding은 아래 명령으로 동기화한다. 대상
디렉터리가 없으면 해당 binding은 native runtime 없음으로 기록한다.

```bash
for dir in bindings/cpp bindings/dotnet bindings/go bindings/java bindings/node bindings/rust; do
  if [ -d "$dir/native/linux-x86_64" ]; then
    cp -f core/build/lib/libzlink.so.* "$dir/native/linux-x86_64/"
    cp -f bindings/c/build/libzlink_c.so.* "$dir/native/linux-x86_64/" 2>/dev/null || true
    find "$dir/native/linux-x86_64" -maxdepth 1 -type f -name 'libzlink.so.*' -print | sort | tail -n 1
  fi
done

cp -f core/include/zlink.h bindings/cpp/include/zlink.h
cp -f core/include/zlink.h bindings/go/include/zlink.h
cp -f core/include/zlink_enum.h bindings/go/include/zlink_enum.h
cp -f core/include/zlink.h bindings/rust/include/zlink.h
```

동기화 뒤 아래 명령으로 stale header를 확인한다.

```bash
diff -u core/include/zlink.h bindings/cpp/include/zlink.h
diff -u core/include/zlink.h bindings/go/include/zlink.h
diff -u core/include/zlink_enum.h bindings/go/include/zlink_enum.h
diff -u core/include/zlink.h bindings/rust/include/zlink.h
```

### 10.3 대상

- `bindings/c`
- `bindings/cpp`
- `bindings/go`
- `bindings/python`
- `bindings/rust`
- `bindings/node`
- `bindings/java`
- `bindings/dotnet`
- 그 밖의 저장소 내 언어별 binding

---

## 11. bindings 라이브러리 반영 게이트

native 동기화가 끝난 뒤에는 각 binding 라이브러리를 새 공개 계약에 맞춰 수정한다.

### 11.1 반영 항목

- `zlink_auto_hwm_profile_t`
- `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY`
- `ZLINK_AUTO_HWM_PROFILE_BALANCED`
- `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT`
- `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`
- `ZLINK_CTX_AUTO_HWM_PROFILE_DFLT`
- context profile set/get wrapper
- monitor snapshot 새 필드
- sample/helper/perf runner의 새 profile option 처리
- C perf runner의 `--auto-hwm-profile` 옵션
- 각 binding perf runner의 profile option 전달 경로

### 11.2 금지 사항

- binding에서 가짜 perf 성공 경로를 추가하지 않는다.
- 실제 결과 없이 0-result smoke를 성공으로 처리하지 않는다.
- binding perf는 최신 core runtime을 쓰는지 확인한 뒤 실행한다.
- binding 문서와 public surface가 서로 다르면 다음 단계로 넘어가지 않는다.

### 11.3 필수 검색 명령

```bash
rg -n "AUTO_HWM_PROFILE|AutoHwmProfile|auto_hwm_profile|autoHwmProfile" bindings
rg -n "ZERO_ON_FAILURE|ZERO_SMOKE|0-result|zero result|fake|가짜" bindings
rg -n "core/build|libzlink|LD_LIBRARY_PATH|native/linux-x86_64" bindings/*/perf bindings/*/tests bindings/*/samples
```

두 번째 검색에서 실제 결과 없는 성공 경로가 발견되면 제거한다.

---

## 12. bindings 검증 게이트

각 binding 라이브러리를 반영한 뒤에는 언어별로 빌드, 테스트, sample, perf 검증을
수행한다.

### 12.1 공통 순서

1. native header/library가 최신 core 기준인지 확인
2. 0.5의 Build/Test 명령 실행
3. 0.5의 Samples 명령 실행
4. 0.5의 Single perf 명령 실행
5. 0.5의 Multi perf 명령 실행
6. profile option set/get 테스트 확인
7. monitor snapshot 새 필드 접근 테스트 확인

### 12.2 최소 확인 대상

0.5에 있는 C, C++, Go, Python, Rust, Node, Java, Dotnet 전체다.

### 12.3 통과 조건

- 각 binding의 Build/Test 명령 성공
- 각 binding의 Samples 명령 성공
- 각 binding의 Single perf 명령 성공
- 각 binding의 Multi perf 명령 성공
- perf 결과 파일에 실제 result row 존재
- profile option set/get 테스트 존재
- monitor snapshot 새 필드 접근 테스트 존재

---

## 13. 최종 완료 조건

이번 작업의 최종 완료 조건은 아래를 모두 만족하는 상태다.

1. draft 내용이 core에 전부 반영되어 있다.
2. 공개 계약이 `core/include/zlink.h`, `core/include/zlink_enum.h` 기준으로 맞다.
3. draft 미적용 항목이 0개다.
4. `core/src` 전체 POSD follow-up이 0개다.
5. core 전체 테스트가 성공한다.
6. `bindings/c/samples` smoke가 성공한다.
7. `bindings/c/perf` single 전체 패턴 smoke가 성공한다.
8. `bindings/c/perf` multi 전체 패턴 smoke가 성공한다.
9. `doc/internals`, `doc/spec`, `doc/guide`, `doc/spec/bindings/언어별/` 문서가
   최신화되어 있다.
10. 각 binding native 폴더가 최신 core header/library 기준으로 동기화되어 있다.
11. 각 binding 라이브러리가 새 API와 enum을 반영한다.
12. 각 binding 전체 테스트가 성공한다.
13. 각 binding의 sample/perf 경로 검증이 완료되어 있다.
14. perf smoke에 가짜 0-result 성공 경로가 남아 있지 않다.

이 조건을 모두 통과하기 전에는 작업을 완료로 보지 않는다.

---

## 14. 진행 로그

### 2026-04-29 계획 문서 생성

- 수정 파일:
  - `doc/plan/monitoring/auto-hwm-context-memory-sizing-rollout-plan.ko.md`
- 실행 명령:
  - `nl -ba doc/plan/spot-refactor/spot-topology-redesign-rollout-plan.ko.md`
  - `nl -ba doc/draft/auto-hwm-context-memory-sizing.ko.md`
  - `find doc/plan -maxdepth 3 -type f`
- 실패 원인:
  - 없음.
- 해결 내용:
  - 기존 SPOT rollout plan의 게이트 구조를 기준으로 auto-HWM v2 적용 계획을
    작성했다.
  - draft의 API/enum, core planner, SpotNode, regression, perf, bindings 조건을
    단계별 통과 조건으로 분리했다.

### 2026-04-29 무인 실행 가능성 보강

- 수정 파일:
  - `doc/plan/monitoring/auto-hwm-context-memory-sizing-rollout-plan.ko.md`
- 실행 명령:
  - `find bindings -maxdepth 3 -type f`
  - `find doc/internals doc/spec doc/guide -maxdepth 3 -type f`
  - `rg -n "CORE_BUILD|core/build|reuse-build|zero|libzlink" bindings/*/perf`
  - `rg -n "^## |^### " doc/plan/monitoring/auto-hwm-context-memory-sizing-rollout-plan.ko.md`
- 실패 원인:
  - 초기 plan에는 core, C perf, binding, 문서 반영 단계의 실제 명령이 부족했다.
  - POSD follow-up 0개 판정이 검색 기준 없이 주관적으로 남아 있었다.
- 해결 내용:
  - 공통 core 명령, C sample/perf 명령, targeted perf 명령을 추가했다.
  - binding별 test, sample, single perf, multi perf 명령을 모두 명시했다.
  - 정식 문서 대상 파일 목록을 명시했다.
  - native 동기화 명령과 stale header 확인 명령을 추가했다.
  - POSD 필수 검색 명령과 종료 조건을 추가했다.
