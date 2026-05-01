# Auto-HWM Per-Connection Profile Rollout Plan

이 문서는 auto-HWM을 context memory budget 기반 계산에서 per-connection profile
기반 계산으로 바꾸기 위한 **실행 계획 문서**다.

설계 기준 draft는 아래 문서다.

- [Auto-HWM per-connection profile draft](../../draft/auto-hwm-per-connection-profile-sizing.ko.md)

이 plan은 구현 순서와 통과 조건을 고정한다. 구현 전에 draft 내용을 정식
`doc/spec`, `doc/guide`, `doc/internals`에 섞지 않는다.

---

## 0. 공통 규칙

1. 공개 계약 기준은 항상 `core/include/zlink.h`와
   `core/include/zlink_enum.h`다.
2. `core/src` 또는 `core/include`를 바꾼 뒤에는 반드시
   `cmake --build core/build`를 실행한다.
3. perf 판단은 항상 `core/build` runtime 기준으로 한다.
4. `bindings/c/perf` runner가 실제 `core/build/lib/libzlink.so...`를 쓰는지
   확인한다.
5. binding 수정 전에는 각 binding의 native header/library를 먼저 동기화한다.
6. fake perf, 0-result 성공, 실제 result row 없는 smoke 성공은 실패로 본다.
7. 기존 사용자 변경은 되돌리지 않는다.
8. 실패한 게이트는 원인을 수정하고 같은 게이트를 다시 실행한다.
9. 각 단계가 끝날 때 `## 14. 진행 로그`에 수정 파일, 실행 명령, 실패 원인,
   해결 내용을 짧게 기록한다.
10. command나 binding 경로가 문서와 다르면 코드 기준으로 실제 경로를 찾아 실행하고,
    plan의 진행 로그에 차이를 남긴다. 경로가 없으면 성공으로 넘기지 말고
    "해당 경로 없음"을 검증 결과로 기록한다.

### 새 컨텍스트 시작 절차

새 agent나 새 context에서 이 plan을 실행할 때는 아래 파일을 먼저 읽는다.

- `AGENTS.md`
- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- `doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
- `doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`

읽은 뒤에는 아래 세 가지만 짧게 보고하고 바로 1단계로 들어간다.

- 읽은 내용 요약
- 현재 단계
- 바로 수정할 파일

이 보고는 확인 요청이 아니다. 실패 게이트, 미반영 항목, 미실행 검증이 남아 있으면
대기하지 않고 다음 수정과 검증을 계속한다.

### 단계 진행 원칙

- plan의 단계 순서를 바꾸지 않는다.
- 한 단계의 통과 조건이 충족되지 않으면 같은 단계에서 원인을 수정하고 다시
  검증한다.
- core/include 또는 core/src를 수정하면 다음 단계로 넘어가기 전에
  `cmake --build core/build`를 실행한다.
- perf 판단은 `bindings/c/perf`가 출력한 실제 `core/build` runtime 경로를 확인한
  뒤에만 한다.
- draft, public header, 정식 문서, binding 중 하나라도 서로 어긋나면 완료로 보지
  않는다.

---

## 1. 현재 상태와 영향 파일 검색

### 작업

- 현재 git 상태를 확인한다.
- auto-HWM memory budget, bootstrap, planning count, fair-share 계산을 모두 찾는다.
- context option enum과 default macro를 찾는다.
- binding별 profile/memory budget 노출 지점을 찾는다.
- perf runner에서 memory budget 옵션을 받거나 출력하는 지점을 찾는다.

### 명령

```bash
git status --short
rg -n "AUTO_HWM_TOTAL_MEMORY|TOTAL_MEMORY_BUDGET|auto_hwm_total_memory|queue_budget|socket_queue_share|planning_count|observed_count|SPOT_BOOTSTRAP|STREAM_BOOTSTRAP" core/include core/src core/tests bindings doc
rg -n "AutoHwm|autoHwm|auto_hwm|AUTO_HWM" bindings
```

### 통과 조건

- 수정 대상 파일 목록이 기록된다.
- 제거할 public option과 유지할 profile option이 분리된다.
- perf runner 변경이 필요한지 확인된다.

---

## 2. 공개 계약 반영

### 설계 기준

- draft [3. 공개 계약 방향](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#3-공개-계약-방향)
- draft [7. monitor snapshot 진단값](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#7-monitor-snapshot-진단값)

### 작업

- draft의 public API / enum 변경 목록을 `core/include/zlink.h`와
  `core/include/zlink_enum.h`에 반영한다.
- draft의 monitor snapshot 공개 필드 정리 기준을 public header에 반영한다.
- 제거한 enum numeric value는 다른 의미로 재사용하지 않는다.
- compatibility 때문에 제거 대신 deprecated를 선택하는 항목은 HWM 계산에 영향을
  주지 않는 no-op인지 테스트로 고정한다.
- 공개 계약 변경 후 binding native header 동기화 대상 목록을 기록한다.

### 수정 파일

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- public header를 vendoring하는 binding include/native 경로

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "test_ctx_options|unittest_ctx_runtime"
```

### 통과 조건

- 제거된 option을 `zlink_ctx_set/get`으로 사용할 수 없거나 deprecated no-op으로
  명확히 동작한다.
- profile set/get은 유지된다.
- 기본 HWM 1000과 auto-HWM opt-in 의미가 테스트로 확인된다.

---

## 3. core planner 재구현

### 설계 기준

- draft [4. 기준 message unit](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#4-기준-message-unit)
- draft [5. Profile별 HWM 기준값](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#5-profile별-hwm-기준값)
- draft [8. 재계산 정책](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#8-재계산-정책)

### 작업

- context memory budget, queue budget, runtime reserve, socket share 계산을 제거한다.
- connection count, observed count, planning count로 HWM을 나누는 경로를 제거한다.
- draft의 per-connection profile table과 message unit scaling을 구현한다.
- 수동 `SNDHWM` / `RCVHWM` override가 자동 계산보다 우선하게 유지한다.
- draft의 재계산 정책에 맞지 않는 connection 변화 기반 재계산 의미를 제거한다.

### 수정 파일 후보

- `core/src/core/auto_hwm_policy.*`
- `core/src/core/ctx.*`
- `core/src/sockets/*auto*hwm*`
- `core/src/services/spot/spot_auto_hwm_internal.hpp`
- 관련 unit/integration tests

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "auto_hwm|hwm|ctx_options|monitor"
```

### 통과 조건

- 같은 profile, 같은 role, 같은 message unit이면 connection 수가 달라도 HWM이
  동일하다.
- message unit이 커지면 HWM은 공식대로 낮아진다.
- STREAM profile HWM은 일반 message socket보다 낮다.
- context memory budget 값이 HWM에 영향을 주는 경로가 없다.
- auto-HWM disabled 상태에서는 기본 HWM 1000이 유지된다.

---

## 3.5 회귀 테스트 구현 게이트

### 설계 기준

- draft [12. 회귀 테스트 항목](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#12-회귀-테스트-항목)

### 추가 또는 갱신할 core 테스트

- draft 12장의 core 회귀 테스트를 모두 추가하거나 기존 테스트에 반영한다.
- 각 테스트는 기대값을 draft 절 링크와 함께 주석 또는 테스트 이름으로 추적 가능하게
  만든다.
- public API 제거/deprecated 선택이 달라진 경우 두 경로 중 실제 선택한 계약만
  테스트한다.

### 추가 또는 갱신할 perf 회귀

```bash
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --runs 1 --duration 2 --clients 100 --msg-sizes 64 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_regress_spot_reqrep_100c_64
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --runs 1 --duration 2 --clients 100 --msg-sizes 64 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_regress_spot_sendsend_100c_64
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT --runs 1 --duration 1 --clients 100 --msg-sizes 64,1024,65536,262144 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_regress_spot_sizes
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM --runs 1 --duration 1 --clients 10000 --msg-sizes 65536 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_regress_stream_10000
```

### 통과 조건

- 회귀 테스트가 core build에 포함된다.
- perf smoke는 실제 result row를 생성한다.
- `SPOT_REQREP`와 `SPOT_SENDSEND`는 100 clients 64B tcp에서 200 Kops/s 이상을
  유지한다.
- STREAM 10,000 clients smoke가 complete 상태와 실제 result row를 출력한다.

---

## 4. SpotNode 적용 게이트

### 설계 기준

- draft [5. Profile별 HWM 기준값](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#5-profile별-hwm-기준값)
- draft [10. 기존 draft와의 관계](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#10-기존-draft와의-관계)

### 작업

- SpotNode 내부 socket의 role/policy class 분리를 유지한다.
- draft의 SpotNode 관련 role/profile 정책을 core helper에 반영한다.
- SpotNode routed delivery queue hard limit는 HWM과 별도 정책으로 유지한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "spot|Spot|unittest_spot"
```

### 통과 조건

- SpotNode 내부 socket HWM이 connection 수나 spot 수로 낮아지지 않는다.
- one-way SPOT large message에서 message unit scaling이 적용된다.
- routed req/resp와 send/send 100 client 64B 기준이 회복 상태를 유지한다.

---

## 5. monitor snapshot 정리

### 설계 기준

- draft [7. monitor snapshot 진단값](../../draft/auto-hwm-per-connection-profile-sizing.ko.md#7-monitor-snapshot-진단값)

### 작업

- draft 7장의 유지/deprecated 기준을 monitor snapshot fill 경로와 public header에
  반영한다.
- ABI 유지 때문에 남기는 deprecated 필드는 draft에 정의된 값 채움 정책을 따른다.
- 공개 enum 없이 내부 enum 숫자가 새 public contract로 남지 않게 한다.
- perf 출력에서 `TotalBudget`, `QueueShare`, `PlanningCount` 같은 오해되는 열을
  제거하거나 deprecated로 표시한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "monitor|snapshot|auto_hwm"
```

### 통과 조건

- monitor snapshot이 새 HWM 계산 근거를 설명한다.
- 제거된 memory budget 필드를 보고 HWM을 해석해야 하는 경로가 없다.
- public snapshot에 내부 enum 숫자나 내부 전이 상태가 새 계약으로 남지 않는다.

---

## 6. core draft 적용 리뷰 게이트

### 작업

- draft의 각 항목을 코드와 public header에 대조한다.
- 아래 항목이 남아 있으면 같은 단계에서 수정한다.
  - memory budget 기반 HWM 계산
  - connection 수 기반 HWM 감소
  - bootstrap count 기반 HWM 감소
  - profile 미반영 role
  - manual HWM override 훼손

### 명령

```bash
rg -n "TOTAL_MEMORY_BUDGET|auto_hwm_total_memory|queue_budget|socket_queue_share|planning_count|observed_count|SPOT_BOOTSTRAP|STREAM_BOOTSTRAP" core/include core/src core/tests
rg -n "AUTO_HWM_PROFILE|auto_hwm_profile|autoHwmProfile" core/include core/src core/tests
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

### 통과 조건

- draft 미적용 항목 0개.
- core 전체 테스트 성공.

---

## 7. POSD 리팩토링 게이트

### 범위

- `core/src` 전체

### 작업

1. 위험 신호를 먼저 열거한다.
2. 각 항목이 어떤 POSD 원칙과 충돌하는지 기록한다.
3. 두 가지 이상 대안을 검토한다.
4. 선택한 방향으로 리팩토링한다.
5. 같은 검사를 반복해 follow-up 0개가 될 때까지 진행한다.

### 집중 항목

- auto-HWM planner와 ctx option dispatch 사이의 정보 누출
- SpotNode auto-HWM helper의 특수/범용 코드 혼합
- monitor snapshot fill 경로의 오래된 budget 지식 누수
- perf/debug 출력 전용 판단이 core 정책에 섞인 경로

### 통과 조건

- `core/src` 전체 POSD follow-up 0개.
- core 전체 테스트 성공.

---

## 8. C sample/perf 검증 게이트

### 명령

```bash
ctest --test-dir bindings/c/build --output-on-failure
bindings/c/samples/run_samples.sh

PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks.sh \
  --pattern ALL \
  --runs 1 \
  --duration 1 \
  --msg-sizes 64 \
  --transports tcp \
  --reuse-build \
  --results-tag auto_hwm_pc_single_smoke

PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern ALL \
  --runs 1 \
  --duration 1 \
  --clients 2 \
  --msg-sizes 64 \
  --transports tcp \
  --reuse-build \
  --results-tag auto_hwm_pc_multi_smoke
```

### targeted perf

```bash
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --runs 1 --duration 2 --clients 100 --msg-sizes 64 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_spot_reqrep_100c_64
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --runs 1 --duration 2 --clients 100 --msg-sizes 64 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_spot_sendsend_100c_64
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT --runs 1 --duration 1 --clients 100 --msg-sizes 64,1024,65536,262144 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_spot_oneway_sizes
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM --runs 1 --duration 1 --clients 1000 --msg-sizes 65536 --transports tcp --auto-hwm-profile balanced --reuse-build --results-tag auto_hwm_pc_stream_1000
```

### 통과 조건

- `bindings/c/perf`가 `core/build` runtime을 사용한다.
- 모든 perf는 실제 result row를 출력한다.
- SPOT_REQREP, SPOT_SENDSEND 100 clients 64B가 200 Kops/s 이상을 회복하거나,
  미달 시 core 원인을 찾아 수정한다.

---

## 9. 정식 문서 반영

구현과 core 검증이 끝난 뒤에만 정식 문서를 수정한다.

### 설계 기준

- draft 전체를 기준으로 삼되, 정식 문서는 AGENTS.md의 디렉터리 목적에 맞게 나누어
  반영한다.
- public API 계약은 구현이 끝난 `core/include/zlink.h`와
  `core/include/zlink_enum.h`만 기준으로 쓴다.

### 수정 대상

- `doc/spec/`
- `doc/guide/`
- `doc/internals/`
- `doc/spec/bindings/*/`
- `doc/site/docs/`

### 반영 내용

- draft의 구현 완료 항목을 정식 문서 성격에 맞게 분배한다.
- `doc/spec/`에는 public header에 존재하는 계약만 반영한다.
- `doc/guide/`에는 사용자 관점의 사용법과 메모리 산정 방법만 반영한다.
- `doc/internals/`에는 core planner 구조와 내부 흐름만 반영한다.
- `doc/spec/bindings/*/`에는 각 언어 public API와 native 동기화 결과만 반영한다.

### 통과 조건

- 정식 spec에는 구현된 public header 내용만 들어간다.
- guide에는 사용자가 메모리를 산정하는 공식이 들어간다.
- internals에는 planner 구조와 role/profile table이 들어간다.
- binding 문서는 각 언어 public API와 제거/deprecated 옵션을 정확히 반영한다.

---

## 10. 정식 문서 적용 리뷰 반복 게이트

### 작업

- draft의 각 절을 정식 문서와 대조한다.
- `doc/spec/`, `doc/guide/`, `doc/internals/`, `doc/spec/bindings/*/`에서 누락,
  과잉 설명, 구현과 불일치한 내용을 찾는다.
- 정식 spec은 `core/include/zlink.h`, `core/include/zlink_enum.h`와 다시 대조한다.
- 누락이나 불일치가 있으면 정식 문서를 수정하고 같은 게이트를 반복한다.

### 명령

```bash
rg -n "AUTO_HWM|auto-HWM|auto_hwm|message unit|memory budget|bootstrap|profile" doc/spec doc/guide doc/internals doc/spec/bindings
rg -n "ZLINK_CTX_OPT_AUTO_HWM|ZLINK_OPT_AUTO_HWM|zlink_auto_hwm|auto_hwm_" core/include/zlink.h core/include/zlink_enum.h
```

### 통과 조건

- draft 구현 완료 항목 중 정식 문서 미반영 항목 0개.
- 정식 spec에 public header에 없는 계약이 없다.
- guide에 내부 구현 세부가 섞여 있지 않다.
- internals에 사용자 사용법 중심 설명이 섞여 있지 않다.
- binding spec이 각 binding public API와 일치한다.

---

## 11. bindings native 동기화와 API 반영

### 순서

1. core runtime rebuild
2. 각 binding native header/library 동기화
3. 각 binding public API에서 memory budget option 제거 또는 deprecated 처리
4. profile option 유지
5. tests/samples/perf 수정

### 검증 명령

각 binding의 기존 test, sample, perf runner를 모두 실행한다.

```bash
bindings/cpp/tests/run_tests.sh
bindings/cpp/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/cpp/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_cpp_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/cpp/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_cpp_multi

bindings/go/tests/run_tests.sh
bindings/go/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bash bindings/go/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_go_single
PERF_DISABLE_RESOURCE_METRICS=1 bash bindings/go/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_go_multi

bindings/python/tests/run_tests.sh
bindings/python/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_python_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_python_multi

bindings/rust/tests/run_tests.sh
bindings/rust/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_rust_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_rust_multi

bindings/node/tests/run_tests.sh
bindings/node/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/node/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_node_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/node/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_node_multi

bindings/java/tests/run_tests.sh
bindings/java/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/java/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_java_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/java/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_java_multi

bindings/dotnet/tests/run_tests.sh
bindings/dotnet/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/dotnet/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_dotnet_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/dotnet/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag auto_hwm_pc_dotnet_multi
```

### 통과 조건

- 모든 binding test 성공.
- sample 디렉터리가 있으면 sample 실행 성공.
- perf 디렉터리가 있으면 실제 result row가 있는 perf smoke 성공.
- memory budget option 제거/deprecated 처리 의미가 binding별로 일치한다.

---

## 12. 최종 문서/API 재대조 게이트

### 작업

- binding 구현과 검증이 끝난 뒤 정식 문서를 다시 대조한다.
- `doc/spec/bindings/*/`가 실제 binding public API와 일치하는지 확인한다.
- `doc/spec/`가 `core/include/zlink.h`, `core/include/zlink_enum.h`와 일치하는지
  다시 확인한다.
- `doc/guide/`의 사용법이 제거/deprecated된 옵션을 새 기능처럼 안내하지 않는지
  확인한다.
- `doc/internals/`가 최종 core 구조와 다른 과거 budget/fair-share 구조를 설명하지
  않는지 확인한다.
- 누락이나 불일치가 있으면 문서를 수정하고 같은 게이트를 반복한다.

### 명령

```bash
rg -n "AUTO_HWM|auto-HWM|auto_hwm|message unit|memory budget|bootstrap|profile" doc/spec doc/guide doc/internals doc/spec/bindings
rg -n "ZLINK_CTX_OPT_AUTO_HWM|ZLINK_OPT_AUTO_HWM|zlink_auto_hwm|auto_hwm_" core/include/zlink.h core/include/zlink_enum.h bindings
```

### 통과 조건

- 정식 문서 미반영 항목 0개.
- public header와 core spec 불일치 0개.
- binding public API와 binding spec 불일치 0개.
- 제거/deprecated 옵션을 활성 기능처럼 설명하는 문서 0개.
- 과거 context memory budget, fair-share, bootstrap 기반 HWM 설명 0개.

---

## 13. 최종 완료 조건

아래 조건이 모두 만족될 때만 완료로 본다.

- draft 내용이 core에 전부 반영됨.
- 공개 계약이 `core/include/zlink.h`, `core/include/zlink_enum.h` 기준으로 맞음.
- context memory budget 기반 HWM 계산 경로 0개.
- connection 수 기반 HWM 감소 경로 0개.
- draft 미적용 항목 0개.
- `core/src` 전체 POSD follow-up 0개.
- core 전체 테스트 성공.
- C samples, C single/multi perf, targeted perf 성공.
- 정식 문서 반영 완료.
- 정식 문서 적용 리뷰 반복 게이트에서 미반영 항목 0개.
- 각 binding native 동기화 완료.
- 각 binding API 반영 완료.
- 각 binding test/sample/perf 검증 완료.
- 최종 문서/API 재대조 게이트에서 미반영 항목 0개.
- perf smoke에 fake/0-result 성공 경로가 없음.

---

## 14. 진행 로그

### 2026-04-30 draft와 rollout plan 작성

- 수정 파일:
  - `doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
  - `doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
- 실행 명령:
  - `sed -n '1,260p' doc/draft/auto-hwm-context-memory-sizing.ko.md`
  - `sed -n '1,220p' doc/plan/monitoring/auto-hwm-context-memory-sizing-rollout-plan.ko.md`
  - `rg -n "AUTO_HWM|auto_hwm|HWM|MEMORY_BUDGET|SPOT_BOOTSTRAP|PROFILE" core/include/zlink.h core/include/zlink_enum.h doc/draft doc/plan/monitoring -g '*.md'`
- 실패 원인:
  - 없음.
- 해결 내용:
  - context memory budget을 제거하고 profile 기반 per-connection HWM으로 재정의하는
    draft를 추가했다.
  - public contract, core planner, SpotNode, monitor, docs, bindings, perf 검증
    순서가 포함된 rollout plan을 추가했다.

### 2026-04-30 plan 역할 정리와 정식 문서 리뷰 게이트 추가

- 수정 파일:
  - `doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
- 실행 명령:
  - `sed -n '1,180p' doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `sed -n '180,380p' doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `sed -n '380,560p' doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `git diff --check -- doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
- 실패 원인:
  - plan에 draft의 설계 세부가 중복되어 draft와 plan이 어긋날 위험이 있었다.
- 해결 내용:
  - plan의 public API, planner, monitor 세부 설명을 draft 절 링크와 구현 지시로
    바꿨다.
  - 개발 완료 뒤 정식 문서를 draft와 public header에 반복 대조해 미반영 항목
    0개까지 수정하는 게이트를 추가했다.

### 2026-04-30 새 컨텍스트 실행성 반복 리뷰

- 수정 파일:
  - `doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
  - `doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
- 실행 명령:
  - `sed -n '1,260p' doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
  - `sed -n '260,560p' doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
  - `sed -n '560,760p' doc/draft/auto-hwm-per-connection-profile-sizing.ko.md`
  - `sed -n '1,260p' doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `sed -n '260,560p' doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `rg -n "<모호한 결정 표현과 금지 표현 패턴>" doc/draft/auto-hwm-per-connection-profile-sizing.ko.md doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `rg -n "<확정되지 않은 변경 표현 패턴>" doc/draft/auto-hwm-per-connection-profile-sizing.ko.md doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
  - `git diff --check -- doc/draft/auto-hwm-per-connection-profile-sizing.ko.md doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
- 실패 원인:
  - 새 context에서 바로 실행하기에는 시작 시 읽어야 할 기준 파일, 최초 보고 형식,
    게이트 실패 시 반복 규칙이 plan에 충분히 명시되어 있지 않았다.
  - draft에 확정되지 않은 변경 표현이 남아 구현자가 판단 대기할 여지가 있었다.
  - binding 구현 후 binding spec과 실제 API를 다시 대조하는 최종 게이트가 없었다.
- 해결 내용:
  - 새 컨텍스트 시작 절차, 단계 진행 원칙, 진행 로그 갱신 규칙을 추가했다.
  - auto-HWM 기본값, bootstrap option, monitor detail flag, role/policy snapshot
    처리 방침을 확정형으로 정리했다.
  - binding 구현 뒤 최종 문서/API 재대조 게이트를 추가했다.

### 2026-04-30 core 공개 계약과 planner 반영

- 수정 파일:
  - `core/include/zlink.h`
  - `core/include/zlink_enum.h`
  - `core/src/core/auto_hwm_policy.hpp`
  - `core/src/core/auto_hwm_policy.cpp`
  - `core/src/core/ctx.hpp`
  - `core/src/core/ctx.cpp`
  - `core/src/sockets/socket_base.cpp`
  - `core/src/sockets/socket_base_monitor.cpp`
  - `core/tests/integration/test_ctx_options.cpp`
  - `core/tests/integration/monitoring/test_monitor_socket_contract.cpp`
  - `core/tests/unittest/unittest_spot_data_plane_budget.cpp`
- 실행 명령:
  - `git status --short`
  - `rg -n "AUTO_HWM_TOTAL_MEMORY|TOTAL_MEMORY_BUDGET|auto_hwm_total_memory|queue_budget|socket_queue_share|planning_count|observed_count|SPOT_BOOTSTRAP|STREAM_BOOTSTRAP" core/include core/src core/tests bindings doc`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "test_ctx_options|unittest_ctx_runtime"`
  - `ctest --test-dir core/build --output-on-failure -R "auto_hwm|hwm|ctx_options|monitor|unittest_spot_data_plane_budget"`
- 실패 원인:
  - 기존 테스트가 auto-HWM 기본 enabled와 context memory budget 기반 기대값을
    전제로 삼고 있었다.
- 해결 내용:
  - auto-HWM 기본값을 disabled로 바꾸고 deprecated memory budget/bootstrap
    option은 set/get 가능하지만 항상 0으로 동작하게 고정했다.
  - planner를 profile, role, message unit 기준의 per-connection HWM 계산으로
    바꿨다.
  - connection 수, observed count, planning count로 HWM을 낮추는 내부 계산 경로를
    제거했다.
  - monitor snapshot의 오래된 budget/planning 필드는 ABI 유지용으로 0을 채우게
    했다.

### 2026-04-30 SpotNode 적용과 POSD 정리

- 수정 파일:
  - `core/CMakeLists.txt`
  - `core/src/services/spot/spot_auto_hwm_internal.hpp`
  - `core/src/services/spot/spot_data_plane_control.cpp`
  - `core/src/services/spot/spot_data_plane_forwarding.cpp`
  - `core/src/services/spot/spot_data_plane_internal.hpp`
  - `core/src/services/spot/spot_data_plane_loop.cpp`
  - `core/src/services/spot/spot_data_plane_pending.cpp`
  - `core/src/services/spot/spot_data_plane_protocol.cpp`
  - `core/src/services/spot/spot_data_plane_runtime.cpp`
  - `core/src/services/spot/spot_mesh_pub_hwm.cpp`
  - `core/src/services/spot/spot_mesh_pub_hwm.hpp`
  - `core/src/services/spot/spot_node.hpp`
  - `core/src/services/spot/spot_node_control.cpp`
  - `core/src/services/spot/spot_node_control_ready.cpp`
  - `core/src/services/spot/spot_node_handles.cpp`
- 실행 명령:
  - `rg -n "spot_mesh_pub_budget|mesh_pub_budget|last_budget_version|budget_version|publish_mesh_pub_budget" core/src core/tests core/CMakeLists.txt`
  - `rg -n "budget|Budget|BUDGET" core/src/services/spot core/CMakeLists.txt`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "auto_hwm|hwm|ctx_options|monitor|spot|Spot|unittest_spot"`
  - `ctest --test-dir core/build --output-on-failure`
- 실패 원인:
  - Spot mesh 내부 이름이 과거 budget 설계를 계속 드러내고 있었다.
  - `test_backpressure_matrix`가 전체 테스트 첫 실행에서 한 번 실패했지만 단독 재실행과
    두 번째 전체 실행에서는 재현되지 않았다.
- 해결 내용:
  - SpotNode helper와 mesh publisher 이름을 HWM 기준으로 정리했다.
  - budget 이름과 connection 수 기반 HWM 감소 의미가 core/src에 남지 않게
    반복 검색했다.
  - rename 이후 `cmake --build core/build`와 core 전체 테스트 100개 통과를 확인했다.

### 2026-04-30 C perf 출력 정리

- 수정 파일:
  - `bindings/c/perf/run_benchmarks.sh`
  - `bindings/c/perf/multi/common/perf_multi_runtime.hpp`
  - `bindings/c/perf/single/common/bench_common_runtime.hpp`
  - `bindings/c/perf/single/run_comparison.py`
  - `bindings/c/perf/run_comparison.py`
  - `bindings/c/perf/README.md`
- 실행 명령:
  - `rg -n "PERF_CTX_AUTO_HWM_TOTAL_MEMORY|ctx_auto_hwm_total_memory|queue_budget|socket_queue_share|planning_count|Auto-HWM budget|spot_budget" bindings/c/perf`
  - `rg -n "0 result|no result|RESULT|result row|result_rows|expected_sizes|summary" bindings/c/perf/run_benchmarks.sh bindings/c/perf/run_benchmarks_multi.sh bindings/c/perf/run_comparison.py bindings/c/perf/single/run_comparison.py`
- 실패 원인:
  - perf runner가 deprecated memory budget env를 넘기고, 상세 표가 queue share와
    planning count를 의미 있는 계산 근거처럼 출력했다.
- 해결 내용:
  - C perf runner에서 memory budget env 전달과 표시를 제거했다.
  - auto-HWM 상세 표를 unit budget, message unit, message slots, applied HWM 중심으로
    바꿨다.
  - single/multi comparison 스크립트가 실제 result line 수가 부족하면 실패하는 것을
    확인했다.
  - 금지 표현과 모호한 결정 표현을 다시 검색했고, 추가 수정이 필요한 항목이
    없음을 확인했다.

### 2026-04-30 C sample/perf 검증

- 수정 파일:
  - `bindings/c/perf/README.md`
- 실행 명령:
  - `cmake --build bindings/c/build`
  - `ctest --test-dir bindings/c/build --output-on-failure`
  - `bindings/c/samples/run_samples.sh`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --reuse-build --results-tag auto_hwm_pc_single_smoke`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --reuse-build --results-tag auto_hwm_pc_multi_smoke`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --runs 1 --duration 1 --clients 100 --msg-sizes 64 --transports tcp --reuse-build`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --runs 1 --duration 1 --clients 100 --msg-sizes 64 --transports tcp --reuse-build`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT --runs 1 --duration 1 --clients 100 --msg-sizes 64,1024,65536,262144 --transports tcp --reuse-build`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM --runs 1 --duration 1 --clients 1000 --msg-sizes 65536 --transports tcp --reuse-build`
- 실패 원인:
  - `ctest`는 등록된 테스트가 없어 `No tests were found!!!`만 출력했다. 이를
    성공 smoke로 해석하지 않고 sample/perf 실제 결과로 검증했다.
  - `bindings/c/perf/README.md`에 예전 context budget tier 설명이 남아 있었다.
- 해결 내용:
  - C sample 10/10 통과를 확인했다.
  - single perf는 30/30, multi perf는 40/40 result line을 확인했다.
  - targeted perf는 SPOT_REQREP 5/5, SPOT_SENDSEND 5/5, SPOT one-way 20/20,
    STREAM 5/5 result line을 확인했다.
  - C perf README를 profile/message-unit sweep 설명으로 바꿨다.

### 2026-04-30 정식 문서와 site 반영

- 수정 파일:
  - `doc/spec/core/context.ko.md`
  - `doc/spec/core/context.md`
  - `doc/spec/core/monitoring.ko.md`
  - `doc/spec/core/monitoring.md`
  - `doc/spec/core/service/spot.ko.md`
  - `doc/spec/core/service/spot.md`
  - `doc/spec/core/socket/stream.ko.md`
  - `doc/spec/core/socket/stream.md`
  - `doc/guide/02-core-api.ko.md`
  - `doc/guide/02-core-api.md`
  - `doc/guide/03-5-stream.ko.md`
  - `doc/guide/03-5-stream.md`
  - `doc/guide/06-monitoring.ko.md`
  - `doc/guide/06-monitoring.md`
  - `doc/guide/10-performance.ko.md`
  - `doc/guide/10-performance.md`
  - `doc/guide/12-socket-options.ko.md`
  - `doc/guide/12-socket-options.md`
  - `doc/internals/socket-option-defaults.ko.md`
  - `doc/internals/socket-option-defaults.md`
  - `doc/internals/spot-internals.ko.md`
  - `doc/internals/spot-internals.md`
  - `doc/site/docs/api/*`
  - `doc/site/docs/guide/*`
  - `doc/site/docs/internals/*`
- 실행 명령:
  - `rg -n "AUTO_HWM|auto-HWM|auto_hwm|message unit|memory budget|bootstrap|profile" doc/spec doc/guide doc/internals doc/site/docs`
  - `cp <updated-doc> <matching-site-doc>`
- 실패 원인:
  - 정식 문서 일부가 auto-HWM enabled 기본값, context memory budget, bootstrap
    planning count, queue share 설명을 오래된 계약처럼 설명했다.
- 해결 내용:
  - spec은 공개 헤더 기준 계약만 남기고 deprecated option과 monitor zero-fill
    의미를 명시했다.
  - guide는 사용자가 볼 동작을 default HWM 1000, opt-in auto-HWM, profile,
    message unit 중심으로 다시 설명했다.
  - internals는 내부 HWM 계산이 context budget이나 connection 수로 나누지
    않는 구조임을 반영했다.

### 2026-04-30 binding native 동기화와 검증

- 수정 파일:
  - `bindings/cpp/include/zlink.h`
  - `bindings/cpp/include/zlink/types.hpp`
  - `bindings/go/include/zlink.h`
  - `bindings/go/include/zlink_enum.h`
  - `bindings/rust/include/zlink.h`
  - `doc/spec/bindings/README.md`
  - `doc/spec/bindings/cpp/README.md`
  - `doc/spec/bindings/dotnet/README.md`
  - `doc/spec/bindings/go/README.md`
  - `doc/spec/bindings/python/README.md`
  - `doc/spec/bindings/rust/README.md`
  - `doc/site/docs/api/bindings.md`
- 실행 명령:
  - `cp core/include/zlink.h bindings/cpp/include/zlink.h`
  - `cp core/include/zlink.h bindings/go/include/zlink.h`
  - `cp core/include/zlink_enum.h bindings/go/include/zlink_enum.h`
  - `cp core/include/zlink.h bindings/rust/include/zlink.h`
  - `bindings/cpp/tests/run_tests.sh`
  - `bindings/go/tests/run_tests.sh`
  - `bindings/python/tests/run_tests.sh`
  - `bindings/rust/tests/run_tests.sh`
  - `bindings/node/tests/run_tests.sh`
  - `bindings/java/tests/run_tests.sh`
  - `bindings/dotnet/tests/run_tests.sh`
  - 각 binding의 `samples/run_samples.sh`
- 실패 원인:
  - 일부 binding spec 문서가 실제 API에 없는 typed memory-budget/bootstrap
    accessor를 설명했다.
- 해결 내용:
  - native header를 공개 header와 동기화했다.
  - binding spec에서 deprecated memory-budget/bootstrap option은 raw enum 또는
    native 호환 no-op으로만 남겼다.
  - C++, Go, Python, Rust, Node, Java, .NET binding test와 sample을 모두 통과했다.

### 2026-04-30 binding perf smoke와 runner 보정

- 수정 파일:
  - `bindings/python/perf/single/run_benchmarks.py`
  - `bindings/java/perf/single/run_benchmarks.sh`
  - `bindings/java/perf/multi/run_benchmarks.sh`
  - `bindings/java/perf/single/Zlink.BindingBench/src/main/java/dev/kairoscode/zlink/perf/single/PerfPair.java`
  - `bindings/java/perf/single/Zlink.BindingBench/src/main/java/dev/kairoscode/zlink/perf/single/PerfDealerDealer.java`
  - `bindings/java/perf/single/Zlink.BindingBench/src/main/java/dev/kairoscode/zlink/perf/single/PerfPubSub.java`
  - `bindings/java/perf/single/Zlink.BindingBench/src/main/java/dev/kairoscode/zlink/perf/single/PerfSpot.java`
- 실행 명령:
  - 각 binding의 `perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp`
  - 각 binding의 `perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp`
- 실패 원인:
  - C++ single PAIR가 한 번 `non_zero_exit_-6`로 실패했으나 같은 runner 경로 재실행에서
    재현되지 않았다. 최종 비디버그 재실행은 35/35 result line으로 통과했다.
  - Python single SPOT_REQREP는 result line을 출력한 뒤 정리 시간이 기본 timeout보다
    길어 runner가 실패했다.
  - Java single DEALER_DEALER는 context 종료 순서가 다른 패턴과 달라
    `zlink_ctx_term`에서 멈췄다.
  - Java single/multi `ALL` 목록에서 SPOT_REQREP가 빠져 있었고, Java multi STREAM은
    명시한 `--msg-sizes 64`를 무시했다.
- 해결 내용:
  - Python single Spot 계열 timeout을 실제 정리 시간에 맞게 늘렸다.
  - Java single PAIR, DEALER_DEALER, PUBSUB, SPOT에 명시적 `ctx.shutdown()`을
    추가했다.
  - Java single/multi `ALL` 목록에 SPOT_REQREP를 포함하고, Java multi STREAM이
    명시된 msg size를 따르도록 고쳤다.
  - C++, Go, Python, Rust, Node, Java, .NET single/multi perf smoke가 모두
    complete 상태와 기대 result line 수로 통과했다.

### 2026-04-30 최종 재대조

- 수정 파일:
  - `doc/plan/monitoring/auto-hwm-per-connection-profile-rollout-plan.ko.md`
- 실행 명령:
  - `rg -n "TOTAL_MEMORY_BUDGET|auto_hwm_total_memory|queue_budget|socket_queue_share|planning_count|observed_count|SPOT_BOOTSTRAP|STREAM_BOOTSTRAP|bootstrap|memory budget|connection count|queue share" core/include core/src core/tests bindings/c/include bindings/cpp/include bindings/go/include bindings/rust/include`
  - `rg -n "AUTO_HWM|auto-HWM|auto_hwm|message unit|memory budget|bootstrap|profile|queue share|planning count|context budget" doc/spec doc/guide doc/internals doc/spec/bindings doc/site/docs`
  - `rg -n "zlink_auto_hwm|AUTO_HWM|auto_hwm_|AutoHwm|autoHwm|memory_budget|total_memory" core/include/zlink.h core/include/zlink_enum.h bindings`
  - `ctest --test-dir core/build --output-on-failure`
  - `ctest --test-dir core/build --output-on-failure -R '^test_zmp_request_reply$' --repeat until-pass:3`
  - `ctest --test-dir core/build --output-on-failure -R '^unittest_service_mode_policy$' --repeat until-pass:5`
  - `ctest --test-dir core/build --output-on-failure`
  - `git diff --check`
- 실패 원인:
  - 최종 문서 검색에서 C perf README의 오래된 context budget 설명이 발견됐다.
  - core full ctest 재확인 중 `test_zmp_request_reply`와
    `unittest_service_mode_policy`가 각각 한 번씩 실패했지만, 두 테스트 모두
    단독 반복에서는 즉시 통과했고 서로 다른 full run에서만 비재현으로 나타났다.
- 해결 내용:
  - C perf README를 profile sweep 설명으로 수정했다.
  - core/src의 memory-budget/planning-count 검색 결과는 deprecated no-op 처리,
    monitor zero-fill, 테스트 assertion, 또는 Discovery/Spot bootstrap 프로토콜
    용어로만 남음을 확인했다.
  - core full ctest를 다시 실행해 100/100 통과를 확인했다.
  - `git diff --check` 통과를 확인했다.
