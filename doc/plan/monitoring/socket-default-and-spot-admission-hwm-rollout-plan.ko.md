# Socket 기본 HWM 및 SPOT Admission HWM Rollout Plan

이 문서는 아래 draft를 코드와 정식 문서, bindings에 반영하기 위한 **실행 계획
문서**다.

- [Socket 기본 HWM 및 SPOT admission HWM draft](../../draft/spot-local-inbound-hwm-policy.ko.md)

이 plan은 설계를 다시 설명하지 않는다. 구현자는 draft를 설계 기준으로 삼고, 이
plan의 순서와 게이트에 따라 사용자 개입 없이 끝까지 진행한다.

---

## 0. 무인 실행 규칙

### 0.1 항상 먼저 읽을 파일

새 context에서 이 plan을 실행할 때는 아래 파일을 먼저 읽는다.

- `AGENTS.md`
- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- `doc/draft/spot-local-inbound-hwm-policy.ko.md`
- `doc/plan/monitoring/socket-default-and-spot-admission-hwm-rollout-plan.ko.md`

읽은 뒤에는 아래 세 가지만 짧게 보고하고 바로 1단계로 들어간다.

- 읽은 내용 요약
- 현재 단계
- 바로 수정할 파일

이 보고는 확인 요청이 아니다. 실패 게이트, 미반영 항목, 미실행 검증이 남아 있으면
대기하지 않고 같은 단계에서 수정과 검증을 반복한다.

### 0.2 공통 규칙

1. 단계 순서를 바꾸지 않는다.
2. 기존 사용자 변경은 되돌리지 않는다.
3. 공개 계약 기준은 항상 `core/include/zlink.h`와 `core/include/zlink_enum.h`다.
4. draft 내용은 구현 전 정식 `doc/spec`, `doc/guide`, `doc/internals`에 섞지 않는다.
5. `core/src` 또는 `core/include`를 바꾼 뒤에는 반드시 `cmake --build core/build`를
   실행한다.
6. perf 판단은 항상 `core/build` runtime 기준으로 한다.
7. `bindings/c/perf` runner가 실제 `core/build/lib/libzlink.so...`를 쓰는지 확인한다.
8. fake perf, 0-result 성공, 실제 result row 없는 smoke 성공은 실패로 본다.
9. sample, perf, test runner가 실제로 없을 때만 `N/A`로 기록할 수 있다. 경로가
   있는데 실패하면 반드시 수정한다.
10. binding 수정 전에는 해당 binding의 native header/library를 최신 core 산출물로
    먼저 동기화한다.
11. 각 단계가 끝날 때 `## 16. 진행 로그`에 수정 파일, 실행 명령, 실패 원인,
    해결 내용을 짧게 기록한다.

### 0.3 공통 명령

```bash
git status --short
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

core 변경 뒤 선별 검증은 최소 아래 패턴을 포함한다.

```bash
ctest --test-dir core/build --output-on-failure \
  -R "ctx_options|auto_hwm|hwm|monitor|spot|Spot|unittest_spot|socket"
```

C sample/perf 공통 smoke는 아래 명령을 기준으로 한다.

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
  --results-tag socket_default_spot_admission_single_smoke

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
  --reuse-build \
  --results-tag socket_default_spot_admission_multi_smoke
```

SPOT targeted perf smoke는 아래 명령을 포함한다.

```bash
PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT \
  --runs 1 \
  --duration 2 \
  --clients 100 \
  --msg-sizes 64,1024,65536,262144 \
  --transports tcp,tls \
  --reuse-build \
  --results-tag socket_default_spot_admission_multi_spot

PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT_REQREP \
  --runs 1 \
  --duration 2 \
  --clients 100 \
  --msg-sizes 64,256,1024 \
  --transports tcp,tls \
  --reuse-build \
  --results-tag socket_default_spot_admission_multi_spot_reqrep

PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT_SENDSEND \
  --runs 1 \
  --duration 2 \
  --clients 100 \
  --msg-sizes 64,256,1024 \
  --transports tcp,tls \
  --reuse-build \
  --results-tag socket_default_spot_admission_multi_spot_sendsend
```

일반 socket auto-HWM smoke는 아래 명령을 포함한다.

```bash
PERF_DISABLE_RESOURCE_METRICS=1 \
bindings/c/perf/run_benchmarks_multi.sh \
  --pattern ALL \
  --runs 1 \
  --duration 1 \
  --clients 2 \
  --msg-sizes 64,1024,65536 \
  --transports tcp \
  --reuse-build \
  --results-tag socket_default_auto_hwm_balanced_multi
```

---

## 1. 현재 상태와 영향 파일 검색

### 설계 기준

- draft [1. 목적](../../draft/spot-local-inbound-hwm-policy.ko.md#1-목적)
- draft [5. public API / enum 영향](../../draft/spot-local-inbound-hwm-policy.ko.md#5-public-api--enum-영향)

### 작업

- git 상태를 확인한다.
- 일반 socket auto-HWM default, context option, socket option 처리 경로를 찾는다.
- SpotNode HWM option, hard limit option, local fanout/routed recv hard-limit disconnect
  경로를 찾는다.
- perf Auto-HWM detail 출력과 snapshot 출력 경로를 찾는다.
- binding별 native header/library 위치와 option 노출 지점을 찾는다.

### 명령

```bash
git status --short
rg -n "AUTO_HWM|SNDHWM|RCVHWM|AUTO_HWM_ENABLE|AUTO_HWM_PROFILE|AUTO_HWM_MSG_UNIT" core/include core/src core/tests bindings doc
rg -n "SPOT_NODE_OPT_.*HWM|QUEUE_HARD_LIMIT|SUB_QUEUE_HARD_LIMIT|ROUTED_QUEUE_HARD_LIMIT|pending.*hard|disconnect" core/include core/src core/tests bindings doc
find bindings -maxdepth 3 -type d \( -name native -o -name include -o -name perf -o -name samples -o -name tests \) | sort
```

### 통과 조건

- 수정 대상 파일 목록이 진행 로그에 기록된다.
- public header, core implementation, core tests, perf, 정식 문서, binding 수정 범위가
  분리된다.
- 현재 dirty worktree에서 사용자 변경과 이번 작업 변경을 구분할 기준을 세운다.

---

## 2. 공개 계약 반영

### 설계 기준

- draft [5.1 추가할 public surface](../../draft/spot-local-inbound-hwm-policy.ko.md#51-추가할-public-surface)
- draft [5.2 변경할 기본값](../../draft/spot-local-inbound-hwm-policy.ko.md#52-변경할-기본값)
- draft [5.3 대체할 SpotNode HWM option](../../draft/spot-local-inbound-hwm-policy.ko.md#53-대체할-spotnode-hwm-option)
- draft [5.4 제거할 hard limit option과 macro](../../draft/spot-local-inbound-hwm-policy.ko.md#54-제거할-hard-limit-option과-macro)

### 작업

- `core/include/zlink.h`와 `core/include/zlink_enum.h`에 draft의 public 변경을
  반영한다.
- 일반 socket 기본 auto-HWM enable default를 draft 기준으로 변경한다.
- 기존 auto-HWM profile enum은 재사용하고 새 profile enum을 추가하지 않는다.
- SpotNode admission option을 public contract에 추가한다.
- 대체/제거되는 SpotNode option 이름을 public header에서 제거한다.
- 제거 또는 대체된 enum 숫자는 예약 상태로 남기고 다른 의미로 재사용하지 않는다.
- SpotNode admission HWM 숫자 override는 양수만 유효하게 한다.
- SpotNode admission HWM option에 `0`을 설정하면 숫자 override를 해제하고 profile
  값으로 돌아가게 한다.
- hard limit default macro를 제거한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "ctx_options|hwm|spot.*option|enum|contract"
```

### 통과 조건

- `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT`가 draft 기준과 일치한다.
- 일반 socket profile enum은 기존 `ZLINK_AUTO_HWM_PROFILE_*`만 사용한다.
- SpotNode admission option이 public header에 있다.
- 제거 대상 option과 macro가 public header에 없다.
- 제거 또는 대체된 enum 숫자가 다른 의미로 재사용되지 않는다.

---

## 3. 일반 socket 기본 auto-HWM 적용

### 설계 기준

- draft [4.4 일반 socket HWM 정책](../../draft/spot-local-inbound-hwm-policy.ko.md#44-일반-socket-hwm-정책)
- draft [5.2 변경할 기본값](../../draft/spot-local-inbound-hwm-policy.ko.md#52-변경할-기본값)

### 작업

- 일반 socket 기본 동작을 auto-HWM enabled + balanced profile로 바꾼다.
- 기존 auto-HWM 계산식, message unit scaling, profile별 min/max cap은 변경하지 않는다.
- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0`이면 일반 socket auto-HWM이 꺼지도록 유지한다.
- 명시적 `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`이 auto-HWM 계산값보다 우선하도록
  유지한다.
- `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`와 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` 입력이 기존
  계산식에 반영되는지 확인한다.

### 수정 시작점

- `core/src/core/ctx.*`
- `core/src/core/auto_hwm_policy.*`
- `core/src/sockets/*`
- `core/tests/integration/test_ctx_options.cpp`
- `core/tests/integration/monitoring/test_monitor_socket_contract.cpp`
- 관련 auto-HWM unit/integration tests

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "ctx_options|auto_hwm|hwm|monitor"
```

### 통과 조건

- context 생성 직후 일반 socket 기본 HWM 모드가 auto-HWM enabled다.
- 기본 profile은 balanced다.
- 일반 socket HWM 값은 기존 계산식으로 산출된다.
- 일반 socket balanced profile이 단순히 16으로 고정되지 않는다.
- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0`은 일반 socket auto-HWM을 끈다.
- 명시 HWM override가 auto-HWM 계산값보다 우선한다.

---

## 4. SpotNode admission HWM 구현

### 설계 기준

- draft [3.1 HWM은 admission control이다](../../draft/spot-local-inbound-hwm-policy.ko.md#31-hwm은-admission-control이다)
- draft [3.3 SpotNode admission 기본값은 profile balanced이다](../../draft/spot-local-inbound-hwm-policy.ko.md#33-spotnode-admission-기본값은-profile-balanced이다)
- draft [4.1 router admission](../../draft/spot-local-inbound-hwm-policy.ko.md#41-router-admission)
- draft [4.2 pubsub admission](../../draft/spot-local-inbound-hwm-policy.ko.md#42-pubsub-admission)
- draft [5.5 Spot HWM 설정 금지](../../draft/spot-local-inbound-hwm-policy.ko.md#55-spot-hwm-설정-금지)

### 작업

- SpotNode public HWM 설정을 `router`, `pubsub` admission 채널로만 노출한다.
- 각 admission 채널의 기본 profile은 balanced로 둔다.
- profile별 admission HWM과 숫자 override 우선순위를 구현한다.
- 숫자 override `0`은 reset으로 처리하고, 양수 override만 실제 HWM 값으로 적용한다.
- 숫자 override reset 뒤에는 해당 채널의 현재 profile HWM이 다시 적용되게 한다.
- Spot 생성 시점에 현재 SpotNode admission HWM을 캡처한다.
- 이미 생성된 Spot에는 이후 SpotNode HWM 변경을 적용하지 않는다.
- Spot send HWM과 SpotNode recv HWM에 같은 admission 값을 적용한다.
- SpotNode send HWM과 Spot recv HWM은 0으로 고정한다.
- Spot handle에는 별도 HWM 설정 API를 추가하지 않는다.
- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0`이 SpotNode admission HWM과 relay/delivery HWM 0
  고정 정책을 바꾸지 않도록 한다.

### 수정 시작점

- `core/src/services/spot/spot_node*`
- `core/src/services/spot/spot_data_plane*`
- `core/src/services/spot/spot_auto_hwm_internal.hpp`
- `core/tests/integration/*spot*`
- `core/tests/unittest/unittest_spot*`

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "spot|Spot|unittest_spot|hwm"
```

### 통과 조건

- router admission profile별 HWM이 draft와 일치한다.
- pubsub admission profile별 HWM이 draft와 일치한다.
- 숫자 override가 profile 값보다 우선한다.
- 숫자 override `0`이 profile 값으로 reset된다.
- 음수 또는 범위를 벗어난 숫자 override는 public option 오류 규칙에 맞게 실패한다.
- 기존 Spot은 SpotNode HWM 변경 후에도 기존 값을 유지한다.
- 새 Spot은 변경 후 값을 캡처한다.
- Spot send와 SpotNode recv가 같은 admission HWM을 가진다.

---

## 5. relay/delivery HWM 0 및 hard limit 제거

### 설계 기준

- draft [3.2 relay와 delivery는 HWM 0으로 고정한다](../../draft/spot-local-inbound-hwm-policy.ko.md#32-relay와-delivery는-hwm-0으로-고정한다)
- draft [4.3 external router와 relay](../../draft/spot-local-inbound-hwm-policy.ko.md#43-external-router와-relay)
- draft [6. hard limit disconnect 제거](../../draft/spot-local-inbound-hwm-policy.ko.md#6-hard-limit-disconnect-제거)

### 작업

- SpotNode 내부 relay, external router, local fanout, mesh relay, final delivery HWM을
  0으로 고정한다.
- direction이 의미 없는 socket 출력은 `-`, direction이 있고 HWM 제한이 없는 경우는
  `0`으로 구분한다.
- per-spot local fanout pending count hard limit disconnect 경로를 제거한다.
- routed recv queue hard limit disconnect 경로를 제거한다.
- byte 단위 staging pause/resume은 disconnect가 아닌 내부 흐름 제어로 유지한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "spot|fanout|routed|dispatch|hwm|budget"
```

### 통과 조건

- external router SNDHWM/RCVHWM snapshot은 0이다.
- local fanout / mesh relay HWM snapshot은 0이다.
- Spot routed/subscriber recv HWM snapshot은 0이다.
- pending count 초과가 target disconnect나 routed recv plane close로 이어지지 않는다.
- dispatch callback 뒤 nonblocking drain이 실제로 가능하다.

---

## 6. core 회귀 테스트 구현 게이트

### 설계 기준

- draft [10. 회귀테스트 기준](../../draft/spot-local-inbound-hwm-policy.ko.md#10-회귀테스트-기준)

### 작업

- draft의 회귀테스트 항목을 core 테스트에 모두 반영한다.
- 테스트 이름이나 주석으로 draft 절을 추적 가능하게 한다.
- public header 제거 항목과 enum 숫자 예약 상태를 테스트나 static check로 검증한다.
- SpotNode admission HWM 숫자 override, reset, invalid value 처리를 테스트한다.
- monitor/perf snapshot에서 `0`과 `-` 구분을 테스트한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "ctx_options|auto_hwm|hwm|spot|monitor|contract"
```

### 통과 조건

- draft 10장의 회귀테스트 항목 중 core에서 검증 가능한 항목이 모두 테스트에 있다.
- 모든 새 테스트가 실패 없이 통과한다.
- 테스트가 fake success나 no-op 검증으로 끝나지 않는다.

---

## 7. draft 전체 적용 확인 게이트 반복

### 작업

- draft 문서를 처음부터 끝까지 다시 읽고, 모든 절의 요구사항을 코드와 대조한다.
- 아래 대조표를 진행 로그에 남긴다.
  - draft 절 번호
  - 요구사항 요약
  - 대응 코드 또는 public header 위치
  - 대응 테스트 위치
  - perf/snapshot 확인 필요 여부
  - 적용 상태: `완료`, `미적용`, `부분 적용`, `해당 없음`
- public contract 항목은 반드시 `core/include/zlink.h`와
  `core/include/zlink_enum.h` 기준으로 확인한다.
- 일반 socket 기본값 변경은 context default, socket 생성 경로, override 경로,
  monitor/perf 출력과 모두 대조한다.
- SpotNode admission HWM은 Spot 생성 시점 캡처, 기존 Spot 미적용, 새 Spot 적용,
  router/pubsub profile/override, override reset, invalid override rejection,
  context auto-HWM 명시 해제와의 독립성을 모두 대조한다.
- relay/delivery HWM `0` 고정은 external router, local fanout, mesh relay,
  Spot routed/subscriber receive 경로별로 대조한다.
- hard limit option 제거는 public header, core implementation, tests, bindings 예정
  반영 목록까지 대조한다.
- 회귀테스트 기준은 draft 10장의 각 행이 실제 core test 또는 이후 perf gate에
  연결되어 있는지 대조한다.
- 미적용, 부분 적용, 검증 부재 항목이 하나라도 있으면 완료로 보지 않는다.
- 미적용 항목이 있으면 관련 구현 단계로 돌아가 수정한다.
- 미적용 항목이 0개가 될 때까지 `build -> test -> review -> fix -> re-review`를
  반복한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "ctx_options|auto_hwm|hwm|spot|monitor|contract"
```

### 통과 조건

- draft 미적용 항목이 0개다.
- draft 부분 적용 항목이 0개다.
- draft 검증 부재 항목이 0개다.
- public header와 draft가 충돌하지 않는다.
- core implementation과 draft가 충돌하지 않는다.
- core tests가 draft의 회귀테스트 기준을 덮는다.
- perf/snapshot 출력 기준이 draft와 일치한다.
- 진행 로그에 draft 절별 대조표가 남아 있다.

---

## 8. POSD 기반 core 리팩토링 게이트 반복

### 기준 문서

- [POSD 설계 원칙](../../principal/software-design-principles.md)

### 작업

- 범위는 `core/src` 전체다.
- 위험 신호를 먼저 열거한다.
- 각 위험 신호에 대해 두 가지 이상 수정 방향을 비교한다.
- 얕은 pass-through, 시간적 분해, 특수/범용 혼합, 정보 누출을 제거한다.
- public API를 불필요하게 넓히지 않는다.
- 리팩토링 후 같은 테스트와 관련 perf smoke를 다시 실행한다.
- 더 진행할 POSD follow-up이 0개가 될 때까지 반복한다.

### 검증

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

### 통과 조건

- `core/src` 전체 POSD follow-up이 0개다.
- 리팩토링이 public contract를 임의로 바꾸지 않는다.
- 전체 core 테스트가 성공한다.

---

## 9. core 전체 검증 게이트

### 작업

- core 전체 build와 test를 실행한다.
- failure, skipped-but-required, flaky 의심 항목을 모두 원인 분석한다.
- source보다 오래된 runtime으로 테스트 또는 perf를 돌리지 않는다.

### 명령

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

### 통과 조건

- core 전체 테스트가 성공한다.
- 실패, timeout, 필수 테스트 skip이 없다.
- 실패 항목을 제외하고 다음 단계로 넘어가지 않는다.

---

## 10. C sample과 perf 검증 게이트

### 작업

- `bindings/c/samples` smoke를 실행한다.
- `bindings/c/perf` single 전체 패턴 smoke를 실행한다.
- `bindings/c/perf` multi 전체 패턴 smoke를 실행한다.
- targeted perf smoke 전체를 실행한다.
- perf runner가 실제 `core/build` runtime을 쓰는지 확인한다.
- Auto-HWM detail 출력이 draft 기준과 일치하는지 확인한다.

### 검증

0.3의 C sample/perf 공통 명령과 targeted perf 명령을 모두 실행한다.

### 통과 조건

- C sample smoke가 성공한다.
- single 전체 패턴 smoke가 실제 result row를 출력한다.
- multi 전체 패턴 smoke가 실제 result row를 출력한다.
- targeted perf smoke가 모두 실제 result row를 출력한다.
- perf 출력에 fake/0-result 성공 경로가 없다.
- SPOT req/resp와 send/send small-message smoke가 회귀 기준을 만족한다.

---

## 11. 정식 문서 반영

### 설계 기준

- draft [11. 문서 반영 방향](../../draft/spot-local-inbound-hwm-policy.ko.md#11-문서-반영-방향)

### 작업

구현과 core 검증이 끝난 뒤에만 정식 문서를 수정한다.

- `doc/internals`에 내부 socket 배선, relay/delivery HWM 0, 일반 socket 기본값 변경을
  반영한다.
- `doc/spec`에 public API, default, enum, 제거 option, option 우선순위를 반영한다.
- `doc/guide`에 사용자 관점의 HWM profile, override, SpotNode admission 사용법을
  반영한다.
- `doc/spec/bindings/언어별/` 문서에 binding별 surface 변경을 반영한다.
- draft 내용을 정식 문서에 그대로 복붙하지 말고 각 디렉터리 독자에 맞게 나눈다.

### 최소 확인 대상

- `doc/internals/socket-option-defaults.ko.md`
- `doc/internals/socket-option-defaults.md`
- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/spec/core/context.ko.md`
- `doc/spec/core/context.md`
- `doc/spec/core/monitoring.ko.md`
- `doc/spec/core/monitoring.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/socket/README.ko.md`
- `doc/spec/core/socket/README.md`
- `doc/spec/core/socket/*.ko.md`
- `doc/spec/core/socket/*.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/10-performance.ko.md`
- `doc/guide/10-performance.md`
- `doc/guide/12-socket-options.ko.md`
- `doc/guide/12-socket-options.md`
- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`
- `doc/spec/bindings/dotnet/README.md`

### 통과 조건

- `doc/internals`, `doc/spec`, `doc/guide`, `doc/spec/bindings/언어별/`가 최신 구현과
  public header 기준으로 맞다.
- guide에 내부 구현 설명을 넣지 않는다.
- spec에는 사용법 설명이 아니라 공개 계약만 둔다.
- internals에는 사용자 사용법이 아니라 내부 구조를 둔다.

---

## 12. binding native 동기화

### 작업

- core build 산출물을 최신 상태로 만든다.
- 각 binding native/include 폴더를 최신 `core/include/zlink.h`,
  `core/include/zlink_enum.h`, `core/build/lib/libzlink.so...` 기준으로 동기화한다.
- 자동 스크립트가 있으면 먼저 사용한다. 없으면 binding별 기존 패턴에 맞춰 동기화한다.
- 동기화 뒤 각 binding이 참조하는 native library 경로를 확인한다.

### 동기화 명령

```bash
cmake --build core/build
bindings/update_zlink_libs.sh
```

### 확인 대상

- `bindings/c/include`
- `bindings/cpp/include`, `bindings/cpp/native`
- `bindings/go/include`, `bindings/go/native`
- `bindings/java/native`
- `bindings/node/native`
- `bindings/python` native 참조 경로
- `bindings/rust/include`, `bindings/rust/native`
- `bindings/dotnet/native`

### 통과 조건

- 각 binding native header/library가 최신 core와 일치한다.
- 오래된 `zlink.h`, `zlink_enum.h`, `libzlink` 복사본이 남아 있지 않다.
- native 동기화 상태가 진행 로그에 기록된다.

---

## 13. binding 라이브러리 반영

### 작업

- 각 binding의 enum/option wrapper를 최신 public contract에 맞춘다.
- 일반 socket 기본 auto-HWM balanced 변경을 binding test와 docs에 반영한다.
- SpotNode admission HWM profile/override option을 binding surface에 반영한다.
- 제거된 SpotNode option과 hard limit option을 binding surface에서 제거한다.
- 제거된 option enum 숫자를 다른 의미로 재사용하지 않는다.
- binding별 sample/perf option parser가 새 option을 지원해야 하면 반영한다.

### 대상 binding

- C
- C++
- Go
- Java
- Node
- Python
- Rust
- Dotnet

### 통과 조건

- 모든 binding이 최신 native header/library와 맞다.
- 제거된 API가 binding에 남아 있지 않다.
- 새 SpotNode admission option이 필요한 binding surface에 노출된다.
- binding 문서와 코드가 public header 기준과 충돌하지 않는다.

---

## 14. binding 검증 게이트

각 binding에 대해 아래 순서를 반복한다. 경로나 runner가 없으면 코드 기준으로 실제
경로를 찾는다. 실제로 없는 경우에만 `N/A`로 기록한다.

### 공통 검증 규칙

- tests가 있으면 모두 실행한다.
- sample 디렉터리가 있으면 실행 확인한다.
- perf 디렉터리가 있으면 실제 result row가 있는 perf smoke를 실행한다.
- 실패하면 같은 binding 단계에서 수정하고 다시 실행한다.
- fake perf, 0-result 성공, 실제 결과 없는 smoke 성공은 실패다.

### C

```bash
ctest --test-dir bindings/c/build --output-on-failure
bindings/c/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --reuse-build --results-tag socket_default_spot_admission_c_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --reuse-build --results-tag socket_default_spot_admission_c_multi
```

### C++

```bash
bindings/cpp/tests/run_tests.sh
bindings/cpp/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/cpp/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_cpp_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/cpp/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_cpp_multi
```

### Go

```bash
bindings/go/tests/run_tests.sh
bindings/go/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/go/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_go_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/go/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_go_multi
```

### Java

```bash
bindings/java/tests/run_tests.sh
bindings/java/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/java/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_java_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/java/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_java_multi
```

### Node

```bash
bindings/node/tests/run_tests.sh
bindings/node/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/node/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_node_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/node/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_node_multi
```

### Python

```bash
bindings/python/tests/run_tests.sh
bindings/python/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_python_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_python_multi
```

### Rust

```bash
bindings/rust/tests/run_tests.sh
bindings/rust/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_rust_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_rust_multi
```

### Dotnet

```bash
bindings/dotnet/tests/run_tests.sh
bindings/dotnet/samples/run_samples.sh
PERF_DISABLE_RESOURCE_METRICS=1 bindings/dotnet/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_dotnet_single
PERF_DISABLE_RESOURCE_METRICS=1 bindings/dotnet/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --results-tag socket_default_spot_admission_dotnet_multi
```

### 통과 조건

- 각 binding 테스트가 성공한다.
- sample이 있는 binding은 sample smoke가 성공한다.
- perf가 있는 binding은 실제 result row가 있는 smoke가 성공한다.
- 실패 또는 미검증 binding이 0개다.

---

## 15. 최종 완료 조건 확인

아래 항목을 모두 만족해야 완료다. 하나라도 실패하면 해당 단계로 돌아가 반복한다.

- draft 내용이 core에 전부 반영됨
- 공개 계약이 `core/include/zlink.h`, `core/include/zlink_enum.h` 기준으로 맞음
- 일반 socket 기본값이 auto-HWM enabled + balanced로 변경됨
- 일반 socket auto-HWM 계산식은 기존 정책 유지
- SpotNode admission HWM이 router/pubsub 채널로 적용됨
- SpotNode admission HWM 숫자 override, reset, invalid value 처리가 반영됨
- relay/delivery HWM 0 고정 정책이 적용됨
- hard limit disconnect option과 동작이 제거됨
- draft 미적용 항목 0개
- `core/src` 전체 POSD follow-up 0개
- core 전체 테스트 성공
- `bindings/c/samples` smoke 성공
- `bindings/c/perf` single 전체 패턴 smoke 성공
- `bindings/c/perf` multi 전체 패턴 smoke 성공
- targeted perf smoke 전체 성공
- `doc/internals`, `doc/spec`, `doc/guide`, `doc/spec/bindings/언어별/` 최신화 완료
- 각 bindings native 동기화 완료
- 각 bindings 라이브러리 반영 완료
- 각 bindings 전체 테스트 성공
- 각 bindings sample/perf 경로 검증 완료
- perf smoke에 fake/0-result 성공 경로가 남아 있지 않음

---

## 16. 진행 로그

진행 중 각 단계마다 아래 형식으로 짧게 기록한다.

```text
### YYYY-MM-DD HH:MM KST - 단계 N
- 수정 파일:
- 실행 명령:
- 실패 원인:
- 해결 내용:
- 결과:
```

### 2026-05-01 - plan 작성

- 수정 파일: `doc/plan/monitoring/socket-default-and-spot-admission-hwm-rollout-plan.ko.md`
- 실행 명령: `rg`, `find`, 기존 plan 문서 확인
- 실패 원인: 없음
- 해결 내용: draft를 코드, 정식 문서, bindings까지 무인 적용하기 위한 단계별 gate 작성
- 결과: plan 초안 작성

### 2026-05-01 00:00 KST - 단계 1

- 수정 파일: 없음
- 실행 명령: `git status --short`, `rg -n "AUTO_HWM|SNDHWM|RCVHWM|AUTO_HWM_ENABLE|AUTO_HWM_PROFILE|AUTO_HWM_MSG_UNIT" core/include core/src core/tests bindings doc`, `rg -n "SPOT_NODE_OPT_.*HWM|QUEUE_HARD_LIMIT|SUB_QUEUE_HARD_LIMIT|ROUTED_QUEUE_HARD_LIMIT|pending.*hard|disconnect" core/include core/src core/tests bindings doc`, `find bindings -maxdepth 3 -type d \( -name native -o -name include -o -name perf -o -name samples -o -name tests \) | sort`
- 실패 원인: 공개 헤더와 SpotNode 구현, core 테스트, 정식 문서, binding native/header에 기존 방향별 SpotNode HWM option과 hard limit option/macro가 남아 있음. `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT`도 draft 기준인 `1`이 아니라 `0`으로 남아 있음.
- 해결 내용: 수정 범위를 public header, context default, SpotNode HWM config/option 처리, relay/delivery HWM 적용, hard limit 제거, core 테스트, C perf runner, 정식 문서, binding native/header와 wrapper로 분리함. dirty worktree의 기존 변경은 되돌리지 않고 누락 항목만 추가 수정하기로 함.
- 결과: 1단계 통과 기준 충족, 2단계 공개 계약 반영으로 진행.

### 2026-05-01 00:00 KST - 단계 2

- 수정 파일: `core/include/zlink.h`, `core/include/zlink_enum.h`, `core/src/services/spot/spot_runtime.hpp`, `core/src/services/spot/spot_node_handles.cpp`, `core/src/services/spot/spot_data_plane_runtime.cpp`, `core/src/services/spot/spot_runtime_attachment.cpp`, `core/src/services/spot/spot_runtime_sender.cpp`, `core/src/services/spot/spot_data_plane_forwarding.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/services/spot/spot_node_summary.cpp`, `core/src/api/service_spot_request_reply_queue.cpp`, `core/src/api/service_spot_request_reply_completion.cpp`, `core/tests/unittest/unittest_spot_data_plane_budget.cpp`, `core/tests/integration/test_ctx_options.cpp`, `core/tests/integration/monitoring/test_monitor_socket_contract.cpp`
- 실행 명령: `cmake --build core/build`, `ctest --test-dir core/build --output-on-failure -R "ctx_options|hwm|spot.*option|enum|contract"`
- 실패 원인: 첫 test run에서 `test_ctx_option_auto_hwm_defaults`가 이전 수동 HWM 기본값 `1000`을 기대했고, `test_pubsub_delivery_ready_snapshot_and_reopen_after_ready`가 auto-HWM disabled snapshot을 기대함.
- 해결 내용: `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT`를 `1`로 변경하고, 기존 SpotNode 방향별 HWM/hard-limit public enum과 macro를 제거했다. 제거 enum 숫자 `0x3608..0x360D`는 예약 주석으로 남기고 새 admission option을 `0x360E..0x3611`에 추가했다. 테스트 기대값을 auto-HWM 기본 enabled + balanced 계산값으로 갱신했다.
- 결과: build 성공, 계약 테스트 9/9 성공.

### 2026-05-01 00:00 KST - 단계 3

- 수정 파일: `core/include/zlink.h`, `core/src/core/ctx.cpp`, `core/src/core/auto_hwm_policy.*`, `core/tests/integration/test_ctx_options.cpp`, `core/tests/integration/monitoring/test_monitor_socket_contract.cpp`
- 실행 명령: `ctest --test-dir core/build --output-on-failure -R "ctx_options|auto_hwm|hwm|monitor"`
- 실패 원인: 없음
- 해결 내용: 일반 socket context 기본값을 auto-HWM enabled + balanced로 확인했다. 명시적 `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0`, profile 변경, message unit 변경, 명시적 socket HWM override는 기존 경로를 유지한다.
- 결과: 일반 socket auto-HWM 관련 테스트 5/5 성공.

### 2026-05-01 00:00 KST - 단계 4

- 수정 파일: `core/src/services/spot/spot_runtime.hpp`, `core/src/services/spot/spot_node_handles.cpp`, `core/src/services/spot/spot_data_plane_runtime.cpp`, `core/src/services/spot/spot_runtime_attachment.cpp`, `core/src/services/spot/spot_runtime_sender.cpp`, `core/src/services/spot/spot_subject_option.cpp`, `core/tests/unittest/unittest_spot_data_plane_budget.cpp`, `core/tests/unittest/unittest_spot_subject_access.cpp`, `core/tests/unittest/unittest_typed_option.cpp`, `core/tests/e2e/spot/test_spot_service_introspection.cpp`
- 실행 명령: `cmake --build core/build`, `ctest --test-dir core/build --output-on-failure -R "spot|Spot|unittest_spot|hwm"`
- 실패 원인: 첫 run에서 기존 `zlink_set_option(..., ZLINK_OPT_RCVHWM)` SpotNode 테스트와 Spot HWM 설정 허용 테스트가 새 계약과 충돌했고, 새 snapshot 테스트가 실제 socket 이름 `local-pub` 대신 `local-fanout`을 찾음.
- 해결 내용: SpotNode HWM config를 router/pubsub admission profile과 숫자 override만 저장하도록 정리했다. pubsub admission은 Spot pub `SNDHWM`과 node `ingress-sub` `RCVHWM`에, router admission은 node `internal-router` `RCVHWM`과 routed sender 생성 경로에 적용했다. Spot/SpotNode 일반 `SNDHWM`/`RCVHWM` common option 경로는 invalid로 막고, 테스트는 `ZLINK_SPOT_NODE_OPT_*` admission option을 사용하도록 변경했다.
- 결과: Spot/HWM 관련 테스트 26/26 성공.

### 2026-05-01 00:00 KST - 단계 5

- 수정 파일: `core/src/services/spot/spot_data_plane_runtime.cpp`, `core/src/services/spot/spot_data_plane_forwarding.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/api/service_spot_request_reply_queue.cpp`, `core/src/api/service_spot_request_reply_completion.cpp`, `core/src/services/spot/spot_node_summary.cpp`, `core/tests/unittest/unittest_spot_subject_access.cpp`
- 실행 명령: `rg -n "ZLINK_SPOT_NODE_OPT_(PUB_HWM|SUB_HWM|ROUTED_SEND_HWM|ROUTED_RECV_HWM|SUB_QUEUE_HARD_LIMIT|ROUTED_QUEUE_HARD_LIMIT)|ZLINK_SPOT_NODE_(SUB|ROUTED)_QUEUE_HARD_LIMIT_DFLT|routed_queue_hard_limit|sub_queue_hard_limit|pending_message_count_hard_limit.*[1-9]|disconnected_targets\\.insert|routed_recv_queue\\.disconnected = true" core/include core/src core/tests`, `ctest --test-dir core/build --output-on-failure -R "spot|fanout|routed|dispatch|hwm|budget"`
- 실패 원인: 없음
- 해결 내용: local fanout pending count hard-limit disconnect와 routed recv queue hard-limit close 경로를 제거했다. byte 단위 pending pause/resume은 유지했고, internal router recv만 router admission HWM을 사용하며 external router, mesh, local fanout, Spot recv 쪽은 HWM 0으로 확인했다.
- 결과: 제거 대상 core 참조 0개, relay/delivery 관련 테스트 27/27 성공.

### 2026-05-01 11:44 KST - 단계 6/7

- 수정 파일: `core/src/services/spot/spot_mesh_pub_hwm.cpp`, `core/src/services/spot/spot_data_plane_runtime.cpp`, `core/src/services/spot/spot_data_plane_forwarding.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/tests/unittest/unittest_spot_data_plane_budget.cpp`, `doc/plan/monitoring/socket-default-and-spot-admission-hwm-rollout-plan.ko.md`
- 실행 명령: `cmake --build core/build`, `ctest --test-dir core/build --output-on-failure -R "ctx_options|auto_hwm|hwm|spot|monitor|contract"`, `rg -n "ZLINK_SPOT_INTERNAL_.*HWM|resolve_internal_hwm_override|ZLINK_SPOT_NODE_OPT_(PUB_HWM|SUB_HWM|ROUTED_SEND_HWM|ROUTED_RECV_HWM|SUB_QUEUE_HARD_LIMIT|ROUTED_QUEUE_HARD_LIMIT)|ZLINK_SPOT_NODE_(SUB|ROUTED)_QUEUE_HARD_LIMIT_DFLT|routed_queue_hard_limit|sub_queue_hard_limit|pending_message_count_hard_limit.*[1-9]|disconnected_targets\\.insert|routed_recv_queue\\.disconnected = true" core/include core/src core/tests`
- 실패 원인: 첫 Stage 6 run에서 `mesh-pub` snapshot이 auto-HWM 계산값을 유지했다. 내부 env HWM override도 admission/relay HWM 고정 정책을 우회할 수 있었다.
- 해결 내용: mesh pub HWM 계산과 refresh를 0 고정으로 바꾸고 테스트 기대값도 0으로 바꿨다. 내부 `ZLINK_SPOT_INTERNAL_*HWM` env override와 사용하지 않는 resolver를 제거해 public SpotNode admission option만 제어면으로 남겼다.
- 결과: core build 성공, Stage 6/7 타깃 테스트 33/33 성공, 제거 대상 core 참조 0개.

Draft 대조 결과:

| Draft 항목 | 반영 상태 |
|---|---|
| 일반 socket 기본 auto-HWM enabled + balanced | 완료 |
| 일반 socket auto-HWM 계산식 유지 | 완료 |
| SpotNode router/pubsub admission option만 공개 | 완료 |
| admission profile 기본값과 8/16/32 매핑 | 완료 |
| 숫자 override, `0` reset, 음수 invalid | 완료 |
| Spot HWM common option 설정 금지 | 완료 |
| 생성 후 HWM 변경은 기존 Spot 미적용 | 완료 |
| router/pubsub admission 양방향 snapshot | 완료 |
| relay/delivery/external/mesh/fanout HWM 0 | 완료 |
| hard-limit disconnect option과 동작 제거 | 완료 |
| snapshot `0`과 `-` 구분 | 완료 |
| 제거 enum 숫자 예약, 재사용 없음 | 완료 |
| draft 미적용 항목 | 0개 |

### 2026-05-01 12:05 KST - 단계 8/9

- 수정 파일: `core/src/api/service_spot_request_reply_internal.hpp`, `core/src/api/service_spot_request_reply_internal.cpp`, `core/src/api/service_spot_request_reply_registry.cpp`, `core/src/api/service_spot_request_reply_queue.cpp`, `core/src/api/service_spot_request_reply_completion.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/services/spot/spot_data_plane_forwarding.cpp`, `core/tests/integration/test_backpressure_matrix.cpp`, `core/tests/integration/test_backpressure_oneway_matrix.cpp`, `core/tests/unittest/unittest_typed_option.cpp`, `doc/plan/monitoring/socket-default-and-spot-admission-hwm-rollout-plan.ko.md`
- 실행 명령: `rg -n "POSD|follow-up|followup|TODO|FIXME" core/src`, `rg -n "//.*(POSD|follow-up|followup)|/\\*.*(POSD|follow-up|followup)" core/src`, `cmake --build core/build`, `ctest --test-dir core/build --output-on-failure`, `ZLINK_TEST_CASE=test_multi_spot_backpressure_matrix timeout 120s core/build/bin/test_backpressure_matrix`, `timeout 120s core/build/bin/test_backpressure_oneway_matrix`, `core/build/bin/unittest_typed_option`, `ctest --test-dir core/build --output-on-failure -R "test_helper_request_sequence_failure"`
- 실패 원인: POSD 점검에서 hard-limit disconnect 제거 뒤에도 routed recv `disconnected` 상태와 local fanout disconnected target 상태가 남아 있었다. 첫 전체 테스트에서는 Spot handle에 직접 HWM을 설정하던 backpressure matrix spot case가 새 계약과 충돌했고, `unittest_typed_option`의 get-option 기대값 한 곳이 기존 domain 결과를 기대했다. 두 번째 전체 테스트에서 `test_helper_request_sequence_failure`가 일시 SIGALRM으로 실패했으나 단일 재현과 다음 전체 run에서는 통과했다.
- 해결 내용: hard-limit disconnect 관련 죽은 상태와 집계 함수를 제거했다. backpressure spot case는 Spot 생성 전에 `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM`을 설정하도록 바꿨고, Spot/SpotNode common HWM get/set은 invalid argument 기대값으로 맞췄다.
- 결과: POSD follow-up 주석 0개, core build 성공, core 전체 테스트 100/100 성공.
