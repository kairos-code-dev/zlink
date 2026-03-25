# POSD 2차 리팩토링 Residual Execution Spec

> 상태: active
> 상위 문서:
> - `doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-gap-review.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md`
> - `doc/plan/refactor/2nd/core-system-posd-refactor-post-residual-review.ko.md`
> 대상 범위: `core/`, `core/tests/`
> 목적: `5.2A`, `5.3A`, `5.6A` residual 항목의 고정 스펙과 post-residual 재진입 판단 기준을 구현자 판단 없이 바로 실행 가능한 수준으로 고정

## 1. 문서 역할

이 문서는 residual 항목 3개를 위한 구현 스펙이다.
설계 방향을 다시 토론하는 문서가 아니라, 실제 구현자가 어떤 책임을 어디로 옮기고
무엇을 남겨야 하는지를 결정 완료 상태로 고정하는 문서다.

현재 시점에서 `5.2A`, `5.3A`, `5.6A`는 `remaining-execution-guide` 기준 완료로 기록돼 있다.
따라서 이 문서는 새 residual을 무한히 추가하는 문서가 아니라,
이미 완료된 residual 경계를 다시 흐리지 않도록 고정하고
후속 perf/구조 작업이 정말 이 residual owner를 다시 열어야 하는지 판단하는 기준으로도 사용한다.

이 문서의 범위는 아래 세 항목으로 제한한다.

- `5.2A` `socket_base_t` residual split
- `5.3A` `ctx_t` runtime orchestration residual split
- `5.6A` service residual deep-module finish

이 문서에서 정하지 않은 사항은 실행 중 새로 결정하지 않는다.
필요하면 이 문서를 먼저 수정한 뒤 구현한다.

## 1.1 현재 적용 상태

현재 워크트리에서 이 문서를 적용하는 방식은 아래처럼 고정한다.

- `5.2A`, `5.3A`, `5.6A`의 owner 경계 정의는 이 문서가 그대로 authority다.
- 이미 완료된 residual 항목을 다시 여는 경우에도, 먼저 이 문서의 owner 범위에 실제로 해당하는지 확인한다.
- owner 범위를 벗어나는 추가 구조-성능 항목은
  `core-system-posd-refactor-post-residual-review.ko.md`를 따른다.
- 즉 이 문서는 residual 구현 스펙이면서 동시에 residual reentry guard다.

## 2. 공통 규칙

모든 residual 항목에 공통으로 적용하는 규칙은 아래처럼 고정한다.

- 공개 C API/ABI는 바꾸지 않는다.
- `core/include/zlink.h`, `core/src/libzlink.vers`는 수정 금지다.
- 새 public class나 새 public header는 추가하지 않는다.
- 새 분리는 private implementation owner를 분리하는 목적에서만 허용한다.
- 파일 수 증가는 허용하지만, helper 나열이 아니라 owner 이동이 보여야 한다.
- 기존 테스트를 약화하지 않는다.
- residual 항목 완료 후 perf로 바로 넘어가기 전에 해당 항목 gate를 먼저 닫는다.
- POSD 판단은 성능 판단을 포함한다.
- 따라서 residual reentry를 허용하려면 구조 owner 경계와 hot-path 비용이 같이 설명돼야 한다.
- 한 번 완료된 residual 항목은 "파일이 다시 커졌다"는 이유만으로 재오픈하지 않는다.
  owner 붕괴, hot-path 재집중, gate 회귀 셋 중 하나가 증거로 있어야 한다.

## 3. `5.2A` `socket_base_t` Residual Split

### 3.1 목표

`socket_base_t`에서 아래 세 축을 semantic facade 밖으로 더 숨긴다.

- public API admission / callback reentrancy / deferred close
- monitor thread/queue 및 event dispatch
- async mailbox quiesce / destroy handoff

### 3.2 `socket_base_t`에 남길 책임

`socket_base_t` 본체에는 아래만 남긴다.

- socket family semantic entrypoint
- virtual contract (`xattach_pipe`, `xsend`, `xrecv` 등)
- family-neutral public surface
- family가 알아야 하는 최소 runtime hook 호출

즉 `socket_base_t`는 "의미 진입점 + collaborator 조정자"로만 읽혀야 한다.

### 3.3 `socket_base_t`에서 빼낼 책임

아래는 `socket_base_t` 본체에서 직접 상태를 들고 있지 않게 만든다.

#### A. lifecycle coordinator

owner:

- public API admission
- callback depth
- close requested / deferred close
- close handoff
- async mailbox quiesce wait

이 owner는 `socket_base_api.cpp`의 admission/close 관련 함수에서 사용하되,
`socket_base_t`는 이 owner의 전체 필드를 참조 멤버로 펼치지 않는다.

#### B. monitor runtime owner

owner:

- monitor socket
- monitor events mask
- lossy flag
- monitor queue
- queue stop flag
- monitor thread start/stop

이 owner는 `monitor()`, `event_*`, `monitor_loop()` 계열에서만 사용한다.

#### C. endpoint/runtime owner

owner:

- endpoint registry
- inproc bookkeeping
- last recv source rid state

이 owner는 endpoint add/remove 및 recv source rid 저장에만 관여한다.

### 3.4 허용되는 파일 경계

이번 항목에서 허용하는 파일 경계는 아래로 고정한다.

- 유지:
  - `core/src/sockets/socket_base.hpp`
  - `core/src/sockets/socket_base.cpp`
  - `core/src/sockets/socket_base_api.cpp`
  - `core/src/sockets/socket_base_endpoint.cpp`
  - `core/src/sockets/socket_base_dispatch.cpp`
  - `core/src/sockets/socket_base_monitor.cpp`
  - `core/src/sockets/socket_base_lifecycle.cpp`
- 새 private 구현 파일 추가 허용:
  - `core/src/sockets/socket_runtime_lifecycle.*`
  - `core/src/sockets/socket_runtime_monitor.*`
  - `core/src/sockets/socket_runtime_endpoint.*`

새 파일은 `socket_base_t`가 직접 필드를 펼치지 않게 숨기는 목적일 때만 추가한다.

### 3.5 금지

- `socket_runtime_t` 이름만 유지한 채 내부 필드만 다른 struct로 다시 옮기는 것
- family 파일이 새 concrete runtime type을 include 하게 만드는 것
- `dealer/router/xpub/xsub/stream`에서 runtime 내부 세부를 직접 읽게 만드는 것
- close contract를 socket family override로 퍼뜨리는 것

### 3.6 완료 조건

아래가 모두 참이어야 완료다.

- `socket_base.hpp`에서 admission/monitor/async mailbox 상세 상태가 대량 참조 멤버로 다시 노출되지 않는다.
- `socket_base_api.cpp`는 lifecycle coordinator API만 알고 내부 atomic 조합은 직접 다루지 않는다.
- family 파일군 수정 없이 대표 socket 회귀가 통과한다.

### 3.7 필수 게이트

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

git diff -- core/include/zlink.h core/src/libzlink.vers
```

## 4. `5.3A` `ctx_t` Runtime Orchestration Residual Split

### 4.1 목표

`ctx_t`를 "global runtime registry + termination contract owner" 수준으로 줄인다.
startup/shutdown/resource creation detail을 `ctx_t` 본체가 전부 직접 아는 구조를 줄인다.

### 4.2 `ctx_t`에 남길 책임

- socket registry owner
- global socket removal wait owner
- inproc endpoint registry owner
- public context contract owner

### 4.3 `ctx_t`에서 숨길 책임

#### A. runtime bootstrap owner

owner:

- reaper 생성/시작
- io_thread 생성/시작
- slots/empty_slots 초기화
- service_control_runtime 부팅

`ctx_t::start()`는 이 owner를 호출만 하고, 세부 절차를 모두 직접 들고 있지 않게 만든다.

#### B. termination sequencing owner

owner:

- terminate/shutdown 공통 stop broadcast
- pending inproc flush 전처리
- reaper completion wait
- restart/reentry-safe termination path

`ctx_t::terminate()`와 `ctx_t::shutdown()`는 semantic entrypoint로 남되,
세부 시퀀스는 helper owner로 이동시킨다.

### 4.4 허용되는 파일 경계

- 유지:
  - `core/src/core/ctx.hpp`
  - `core/src/core/ctx.cpp`
- 새 private 구현 파일 추가 허용:
  - `core/src/core/ctx_bootstrap.*`
  - `core/src/core/ctx_termination.*`

새 owner는 `ctx_t` private collaborator로만 사용한다.

### 4.5 금지

- `ctx_t`를 facade로 만들겠다며 registry 책임까지 외부로 빼는 것
- `service_control_runtime` 생명주기를 service 계층으로 올려버리는 것
- socket removal wait contract를 `service_runtime_base_t` 쪽으로 밀어 넣는 것

### 4.6 완료 조건

- `ctx.cpp`가 startup/shutdown/resource detail의 단일 허브처럼 읽히지 않는다.
- `ctx_t` 본체 설명이 "registry + termination contract owner" 수준으로 줄어든다.
- self-close / service lifecycle 회귀가 기존 계약으로 유지된다.

### 4.7 필수 게이트

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

git diff -- core/include/zlink.h core/src/libzlink.vers
```

## 5. `5.6A` Service Residual Deep-Module Finish

### 5.1 목표

아래 3개 large file을 helper 분할이 아니라 owner 재배치로 마감한다.

- `registry.cpp`
- `spot_subject_access.cpp`
- `spot_data_plane.cpp`

### 5.2 `registry.cpp`

남길 책임:

- `registry_t` public/service facade entry

분리할 owner:

- registry state/rules
- socket ensure/replace
- topology/gateway-peer query reply assembly

허용되는 새 private 파일:

- `core/src/services/discovery/registry_runtime.*`
- `core/src/services/discovery/registry_query.*`
- `core/src/services/discovery/registry_state.*`

금지:

- protocol encode/decode를 다시 `registry.cpp`에 합치는 것
- query filter와 socket ensure 로직을 같은 owner에 두는 것

### 5.3 `spot_subject_access.cpp`

남길 책임:

- subject facade seam

분리할 owner:

- subject publish
- subject query / snapshot access
- subject poller socket resolution
- send-ready / dispatch helper seam

허용되는 새 private 파일:

- `core/src/services/spot/spot_subject_publish.*`
- `core/src/services/spot/spot_subject_query.*`
- `core/src/services/spot/spot_subject_poller.*`

금지:

- poller resolution, publish, query를 다시 하나의 giant access file로 묶는 것

### 5.4 `spot_data_plane.cpp`

남길 책임:

- `spot_data_plane_t` semantic entry

분리할 owner:

- runtime assembly/wiring
- forwarding path
- budget/control carryover policy

허용되는 새 private 파일:

- `core/src/services/spot/spot_data_plane_runtime.*`
- `core/src/services/spot/spot_data_plane_budget.*`

금지:

- perf workaround를 이유로 budget policy와 forwarding path를 다시 한 파일에 억지로 합치는 것
- benchmark-specific 조건문을 product path에 직접 넣는 것

### 5.5 완료 조건

- 각 large file owner를 한 문장으로 설명할 수 있다.
- `gateway`, `discovery`, `spot` full service core smoke가 유지된다.
- residual service 정리 뒤 perf owner 추적이 더 좁은 파일 집합으로 설명된다.

### 5.6 필수 게이트

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'

ctest --test-dir core/build --output-on-failure -R \
'^(unittest_service_mode_policy|unittest_spot_subject_access|unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'

git diff -- core/include/zlink.h core/src/libzlink.vers
```

## 6. post-residual 진입 및 재진입 규칙

`5.7` perf 회복 루프에 다시 진입하려면 아래 셋이 모두 완료여야 한다.

- `5.2A` 완료
- `5.3A` 완료
- `5.6A` 완료

이 조건을 만족하기 전에는 perf targeted recheck를 새 authority로 승격하지 않는다.
기존 perf 로그는 historical evidence로만 사용한다.

현재는 위 세 항목이 이미 완료된 상태이므로,
추가 구현은 아래처럼 구분한다.

- 증상이 `socket_base_t`, `ctx_t`, `registry/spot residual owner`의 붕괴로 직접 설명되면
  이 문서의 해당 residual 항목으로 재진입한다.
- 증상이 현재 active owner인 `spot` secure multi-peer `mesh_pub` hot path,
  `spot_data_plane_internal.hpp`,
  `spot_data_plane_runtime.cpp`,
  `spot_node_control.cpp` 경계로 더 잘 설명되면
  residual 재오픈이 아니라 post-residual 작업으로 진행한다.
- post-residual 작업의 실제 착수 순서와 체크리스트는
  [remaining-execution-guide의 `5.7A`~`5.7D`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57a-post-residual-기준-spot-secure-multi-peer-current-owner-마감)
  를 따른다.
- `socket_message_api.cpp`, `options_t`, `spot_node_t`, `discovery_t`,
  engine/transport owner 정리는 이 문서 범위 밖이며
  [post-residual-review의 우선순위/순서/완료 기준](./core-system-posd-refactor-post-residual-review.ko.md#8-코드-전반-우선순위-재판정)
  를 따른다.

즉 현재 단계에서 이 문서의 역할은
"남은 모든 구조 작업을 담는 목록"이 아니라
"이미 닫은 residual owner를 다시 열어야 하는지 판정하는 엄격한 기준"이다.

## 7. 구현 순서

residual 자체를 처음 수행할 때의 구현 순서는 아래로 고정한다.

1. `5.2A`
2. `5.3A`
3. `5.6A`
4. `5.7`

이 순서를 바꾸려면 먼저 이 문서를 수정해야 한다.

현재 워크트리처럼 `5.2A`, `5.3A`, `5.6A`가 이미 완료된 단계에서는
이 절을 현재 실행 순서로 해석하지 않는다.
현재 active 구현 순서는
[remaining-execution-guide의 `5.7A`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57a-post-residual-기준-spot-secure-multi-peer-current-owner-마감),
[`5.7B`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57b-post-residual-기준-socket_message_apicpp--optionst-ownership-재정리),
[`5.7C`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57c-post-residual-기준-spot_nodet--discoveryt-facade-구조-마감),
[`5.7D`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57d-post-residual-기준-engine--transport-owner-재판정),
[`5.7`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57-coreperf-smoke--full-baseline-비교--성능-회복-루프)
을 따른다.

## 8. 반복 리뷰 절차

현재처럼 residual 완료 후에도 구조-성능 이슈를 반복 리뷰할 때는
아래 절차를 고정한다.

1. latest 실행 로그와 exact tuple 비교에서 current worst tuple을 먼저 고정한다.
2. current owner가 이 문서의 `5.2A`, `5.3A`, `5.6A` owner 경계 중 어디에 속하는지 판정한다.
3. 속하지 않으면 residual 재오픈을 금지하고 `post-residual-review` 작업 목록으로 보낸다.
   작업 목록은
   [remaining-execution-guide의 `5.7A`~`5.7D`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57a-post-residual-기준-spot-secure-multi-peer-current-owner-마감)
   와
   [post-residual-review의 추가 작업 순서](./core-system-posd-refactor-post-residual-review.ko.md#9-현재-성능-개선을-마무리하기-위한-추가-작업-순서)
   를 함께 본다.
4. 속하면 해당 residual 항목의 "남길 책임 / 빼낼 책임 / 금지 / 필수 게이트"를 그대로 다시 적용한다.
5. 수정 후에는 관련 gate와 ABI 무변경 확인을 먼저 닫고,
   그 다음에만 perf recheck를 증거로 추가한다.

이 절차를 건너뛰고 "현재 perf가 안 좋으니 residual을 다시 열자"는 식으로 판단하는 것은 금지한다.

## 9. 현재 active handoff

2026-03-25 현재 latest 로그 기준 active issue는
residual spec 자체보다 `spot` secure multi-peer `mesh_pub` hot path 쪽으로 좁혀져 있다.

현재 immediate next step은 아래처럼 고정한다.

- `spot_data_plane_internal.hpp`의 live budget / carryover 정책
- `spot_data_plane_runtime.cpp`의 secure steady-state send path
- `spot_node_control.cpp`의 ready-peer driven budget application

현재 증거만으로는 `5.2A`, `5.3A`, `5.6A`를 다시 열 근거가 확정되지 않았다.
따라서 이 문서를 읽고 바로 작업할 때도,
첫 판단은 "residual 재오픈"이 아니라 "post-residual current owner 추적"이어야 한다.
현재 current owner 정의와 immediate next step은
[post-residual-review의 active issue 절](./core-system-posd-refactor-post-residual-review.ko.md#3-실행-로그-기준-현재-active-issue)
과
[remaining-execution-guide의 `5.7A`](./core-system-posd-refactor-remaining-execution-guide.ko.md#57a-post-residual-기준-spot-secure-multi-peer-current-owner-마감)
를 함께 기준으로 삼는다.

residual 재오픈은 아래 셋 중 하나를 로그와 코드로 함께 설명할 수 있을 때만 허용한다.

- `socket_base_t` lifecycle/monitor/endpoint owner가 다시 hot-path에 재집중됐다.
- `ctx_t` bootstrap/termination detail이 registry contract owner를 다시 오염시켰다.
- `registry.cpp`, `spot_subject_access.cpp`, `spot_data_plane.cpp` 계열에서
  owner 이동이 무너지고 giant helper 또는 cross-owner coupling이 다시 생겼다.
