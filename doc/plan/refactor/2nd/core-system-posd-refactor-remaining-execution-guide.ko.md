# POSD 2차 리팩토링 잔여 작업 실행 가이드

> 상태: active
> 기준 문서: `doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md`
> 재평가 문서: `doc/plan/refactor/2nd/core-system-posd-refactor-gap-review.ko.md`
> residual 실행 스펙: `doc/plan/refactor/2nd/core-system-posd-refactor-residual-execution-spec.ko.md`
> post-residual 재리뷰: `doc/plan/refactor/2nd/core-system-posd-refactor-post-residual-review.ko.md`
> 기준 커밋: `85ea0995`
> 대상 범위: `core/`, `core/tests/`
> 범위 해석 고정: 이 실행 문서의 완료 조건과 중간 게이트는 `core/`와 `core/tests/`만으로 닫는다. `bindings/*`는 별도 후속 검증 surface로 두며, 이 문서의 진행 차단 조건으로 사용하지 않는다.
> 성능 개선/검증: 이 문서의 active 범위 밖. 기존 perf 기록은 archive로만 유지하고, 완료/중간 게이트에는 사용하지 않는다.
> 목적: 남은 POSD 2차 리팩토링 항목을 중간 중단 없이 끝까지 밀기 위한 실행 규칙 고정

## 1. 문서 목적

이 문서는 마스터 플랜의 "남은 작업"만을 대상으로,
실제 구현자가 어떤 순서와 어떤 정지 규칙으로 끝까지 진행해야 하는지를 고정한다.

이 문서는 새 설계를 제안하는 문서가 아니다.
이미 반영된 2차 리팩토링 결과 위에서,
남은 허브와 남은 ownership 정리를 끝까지 닫는 실행 문서다.

2026-03-24 현재 워크트리 재리뷰 기준으로
`core-system-posd-refactor-gap-review.ko.md`를 추가 authority로 사용한다.
이 실행 문서는 해당 갭 리뷰를 반영해, 남은 구조 갭을 닫고
리팩토링 완료 상태를 `core/`와 `core/tests/`로만 판정하는 순서를 고정한다.

`5.2A`, `5.3A`, `5.6A`의 세부 구현 결정은
`core-system-posd-refactor-residual-execution-spec.ko.md`를 단일 authority로 사용한다.
실행 중 해당 세 항목의 owner/file/type 경계를 새로 판단하지 않는다.

residual 이후 추가 구조 작업의 우선순위와 완료 해석은
`core-system-posd-refactor-post-residual-review.ko.md`를 후속 authority로 사용한다.
특히 post-residual 기준으로 다시 드러난 facade/owner 잔여 항목은
해당 문서의 current owner 해석과 작업 순서를 따른다.

핵심 원칙은 아래 두 줄이다.

```text
남은 항목은 하나씩 닫되, 진행 보고를 이유로 작업을 멈추지 않는다.
실제 중단은 재현 가능한 hard blocker, ABI 계약 위험, 사용자 판단 필요 상황에서만 허용한다.
```

## 2. 현재 기준선

현재 기준선은 아래처럼 본다.

- `api/zlink.cpp`는 이미 thin facade에 가까워졌다.
- `gateway`, `discovery`는 본체가 크게 줄었고 여러 deep module로 분리됐다.
- `spot subject` API는 publish/recv, option/routing/tls, subscription/query로 분리됐다.
- `options_t` owner map은 시작됐지만 완결은 아니다.
- `service_runtime_base_t` / `socket_close_ops_t` / `ctx_t` close-wait 분업은 실제로 들어갔다.
- 다만 갭 리뷰 기준으로 `socket_base_t`, `ctx_t`, 일부 `spot`/`discovery` deep module은 아직 더 정리해야 한다.
- 이 문서의 완료 판정은 구조 설명과 `core/tests` 검증으로만 닫는다.

현재 남은 큰 허브는 대략 아래다.

- `core/src/api/zlink.cpp`
- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/core/ctx.cpp`
- `core/src/services/discovery/registry.cpp`
- `core/src/services/spot/spot_subject_access.cpp`
- `core/src/services/spot/spot_data_plane.cpp`

## 3. 중단 금지 규칙

이 문서에 따라 작업할 때는 아래 원칙을 따른다.

1. 진행 상황 공유를 이유로 작업을 멈추지 않는다.
2. 한 항목이 끝나면 바로 다음 항목으로 넘어간다.
3. 중간 상태 설명, 부분 요약, 임시 완료 보고는 하지 않는다.
4. 각 항목은 "코드 수정 + 관련 회귀 검증"까지 포함해야 닫힌다.
5. 테스트가 깨지면 같은 흐름 안에서 원인까지 바로 해결한다.
6. 한 항목을 `완료`로 바꾼 직후에는 다음 미완료 항목으로 즉시 이동한다.
7. 사용자의 새 지시가 없으면 "여기까지"를 이유로 세션을 끝내지 않는다.

다만 아래 경우에만 즉시 멈추고 사용자 판단을 요청할 수 있다.

- 로컬 회귀 테스트와 코드 읽기만으로는 결론을 낼 수 없는 C API/ABI 계약 충돌이 발생한 경우
- 사용자 작업과 직접 충돌해서 임의 진행이 위험한 경우
- `core/include/zlink.h` 또는 `core/src/libzlink.vers` 변경이 필요한 경우
- `core/`와 `core/tests/`만으로는 해결할 수 없는 blocker가 생긴 경우

위 네 경우가 아니면 멈추지 않는다.

중단 전 확인 순서는 아래처럼 고정한다.

1. `core/tests/`에 같은 현상을 재현하는 회귀가 이미 있는지 확인한다.
2. 없으면 같은 계약을 때리는 더 작은 회귀를 먼저 추가한다.
3. 그 회귀까지 `core/` 수정으로 해결 가능하면 계속 진행한다.
4. 그래도 `core/`와 `core/tests/` 범위만으로 해결이 불가능할 때만 멈춘다.

중단이 허용되지 않는 상황은 아래처럼 고정한다.

- 특정 phase의 일부 테스트만 green인 상태
- 줄 수 감소나 파일 분리만 확인된 상태
- 미완료 표의 다음 행이 남아 있는 상태
## 4. 반복 실행 루프

남은 작업은 아래 루프로 끝까지 반복한다.

1. 미완료 항목 목록에서 가장 위의 항목 하나를 고른다.
2. 그 항목의 허브를 더 작은 ownership 단위로 분리한다.
3. 바로 관련 회귀 테스트를 추가 또는 갱신한다.
4. 빌드와 해당 회귀 묶음을 통과시킨다.
5. 문서의 미완료 목록에서 해당 항목을 줄이거나 제거한다.
6. 남은 항목이 없을 때까지 1로 돌아간다.

항목 종료 후 즉시 수행 규칙:

1. 방금 끝낸 항목의 상태를 5.0 표에 반영한다.
2. 5.0 표에서 `완료`가 아닌 첫 행을 다시 찾는다.
3. 그 행이 존재하면 즉시 그 행의 작업으로 넘어간다.
4. 5.0 표가 전부 `완료`일 때만 6.3 최종 게이트로 간다.

금지 규칙은 아래와 같다.

- "일단 분리만 하고 테스트는 나중에" 금지
- "한 번에 여러 phase를 섞어서 원인 추적이 불가능하게 만드는 변경" 금지
- "테스트를 약화해서 green만 만드는 수정" 금지
- "문서와 코드의 ownership 설명이 더 나빠지는 얇은 wrapper 추가" 금지
- "나중에 성능을 볼 예정이니 구조 설명과 테스트 증거를 생략한다" 금지

## 4.1 공통 실행 명령

아래 명령은 실행 문서의 기본 명령으로 고정한다.

```bash
cmake --build core/build -j"$(nproc)"

git diff -- core/include/zlink.h core/src/libzlink.vers
nm -D core/build/lib/libzlink.so | rg " zlink_"

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

./core/tests/run_test_lanes.sh --include-e2e
```

`thread-safe stress`의 기본/최소 반복 횟수는 `10`으로 둔다.
AI가 flake 재현, 신뢰도 보강, 추가 회귀 확인이 더 필요하다고 판단하면 `10`보다 큰 count를 사용할 수 있다.

bindings smoke는 이 실행 문서의 범위 밖이다.

- `bindings/*` 검증은 별도 후속 문서 또는 별도 세션에서 다룬다.
- 이 문서의 phase 종료, 최종 완료, 중단 판정은 `core/`와 `core/tests/` 증거로만 닫는다.
- bindings 관련 blocker는 `core/include/zlink.h` 또는 `core/src/libzlink.vers` 변경 필요가 없는 한 이 문서의 진행 차단 조건으로 승격하지 않는다.

대표 core 테스트 묶음은 아래처럼 고정한다.

```bash
ctest --test-dir core/build --output-on-failure -R \
'^(unittest_typed_option|unittest_poller|unittest_service_mode_policy)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_monitor_socket_contract|test_monitor_service_contract|test_monitor_with_handler)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_spot_pubsub_scenario|test_spot_service_introspection)$'
```

해석 규칙:

- `gateway 대표 integration`은 `test_gateway_with_handler|test_gateway_handover`
- `discovery 대표 integration`은 `test_service_discovery|test_service_introspection`
- `spot 대표 integration/e2e`는 `test_spot_pubsub_scenario|test_spot_service_introspection`
- `대표 socket 회귀`는 `test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup`
- `대표 option 회귀`는 `unittest_typed_option`

대표 core API smoke 명령은 아래처럼 이름으로 고정한다.

- `대표 core API smoke`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'`
- `full service core smoke`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'`

기존 `core/perf` 실행 규칙, baseline 비교 규칙, worst-tuple 산출 규칙, targeted/full recheck 규칙은
archive로만 남긴다.

- 이 문서의 active loop에서는 `core/perf`를 새 증거로 요구하지 않는다.
- 이미 남아 있는 perf 로그/비교 기록은 과거 판단 근거로만 참고한다.
- 성능 개선 루프, baseline 회복 판정, tuple 우선순위 산출은 별도 후속 문서에서 다시 다룬다.

## 5. 남은 작업 체크리스트

아래 체크리스트를 위에서부터 순서대로 닫는다.
각 항목은 체크 직전까지 실제 코드와 검증으로 닫혀 있어야 한다.

## 5.0 마스터 플랜 커버리지 추적 표

아래 표는 `core-system-posd-refactor-master-plan.ko.md`의 남은 항목이
실행 문서에서 빠지지 않도록 강제로 추적하는 표다.

상태 값은 아래 네 개로만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

상태 전이 규칙은 아래처럼 고정한다.

- `미착수 -> 진행중`: 해당 항목 owner를 옮기는 코드 수정을 시작한 시점
- `진행중 -> 검증중`: 관련 빌드와 해당 항목 검증 명령이 모두 통과한 시점
- `검증중 -> 완료`: 검증 증거 파일/명령/로그 위치까지 표에 기록한 시점
- `완료 -> 진행중`: 이후 변경으로 해당 항목 owner 또는 검증 결과가 다시 흔들린 시점

세션 시작 시 첫 행동은 아래처럼 고정한다.

1. 5.0 표를 먼저 읽는다.
2. 갭 리뷰 문서의 최신 평결이 5.0 표에 반영됐는지 확인한다.
3. `완료`가 아닌 첫 행을 이번 세션의 우선 작업으로 잡는다.
4. 해당 행의 `실행 문서 체크 항목`과 `검증 증거`를 먼저 확인한다.
5. 증거가 비어 있으면 `완료`로 간주하지 않는다.
6. 첫 행이 `검증중`이면 검증 증거를 먼저 채우고 그 다음 줄로 넘어간다.

완료로 바꾸려면 아래 세 칸이 모두 채워져야 한다.

- `실행 문서 체크 항목`
- `관련 코드/파일`
- `검증 증거`

### 5.0.1 장시간 게이트 대기 규칙

아래처럼 수 분 이상 걸릴 수 있는 장시간 검증은 기본적으로 현재 작업 세션에 붙여 실행한다.
콘솔 출력은 이 작업 세션에 계속 보여야 하며, 동시에 로그 파일에도 저장한다.

- `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10` (기본/최소)
- `full service core smoke`

기본 실행 방식:

- 장시간 검증은 본 실행 전에 먼저 `사전 검증(preflight)`을 통과해야 한다.
- `사전 검증`은 최소한 build 디렉터리의 CTest 구성 존재 여부와, 장시간 검증이 호출할 테스트 이름이 현재 CTest에 모두 등록돼 있는지 확인해야 한다.
- `사전 검증`이 실패하면 본 실행을 시작하지 않고 즉시 실패로 처리한 뒤 스크립트/등록 상태를 먼저 수정한다.
- 저장소 로컬 wrapper를 써도 되지만, wrapper는 내부에서 원래 gate 명령을 바꾸지 않고 그대로 호출해야 하며 같은 콘솔/로그 규칙을 유지해야 한다.
- 문서 전체 실행을 Codex supervisor로 감싸는 것은 허용한다. 다만 supervisor는 각 iteration에서 이 문서를 그대로 authority로 사용해야 하고, 완료 판정은 반드시 이 문서 기준으로만 내려야 한다.
- 장시간 검증은 `... 2>&1 | tee <log>` 형태로 실행해서 콘솔 출력과 로그 파일 기록을 동시에 유지한다.
- 장시간 검증을 시작했다면 그 세션은 해당 검증 완료 확인까지 유지한다.
- 백그라운드 실행은 사용자 명시 요청이나 세션 제약이 있는 예외 상황에서만 허용한다.

규칙:

- 해당 행의 구현 변경이 끝났고 남은 일이 장시간 검증 하나뿐이면, 같은 행에 머문 채 검증을 시작한다.
- 장시간 검증 시작 절차는 `사전 검증 성공 -> 본 실행 시작` 순서로 고정한다.
- `thread-safe stress` 반복 횟수는 기본/최소 `10`으로 시작하고, AI가 필요하다고 판단하면 더 큰 count로 올릴 수 있다.
- 장시간 검증을 시작한 뒤에는 다른 미완료 행으로 넘어가지 않는다.
- 장시간 검증이 foreground로 붙어 있는 동안에는 콘솔 출력 자체를 진행 증거로 간주한다.
- 이 대기 중에는 `pgrep`, `ps`, `tail`, 로그 파일 확인처럼 완료 여부를 판단하기 위한 확인 작업만 허용한다.
- 완료 여부 확인은 주기적으로 반복할 수 있으며, 이는 중간 완료 보고나 순서 위반으로 간주하지 않는다.
- 장시간 검증이 성공하면 즉시 5.0 표와 해당 체크리스트를 갱신하고 다음 미완료 행으로 이동한다.
- 장시간 검증이 실패하면 실패 로그를 증거로 남기고 같은 행을 계속 owner로 유지한 채 원인 수정으로 되돌아간다.
- 장시간 검증 실패 후에는 `실패 보고 -> 중단`으로 끝내지 않는다.
- 실패한 장시간 검증은 아래 순서를 끝까지 수행한 뒤에만 다음 미완료 행으로 넘어갈 수 있다.

실패 후 의무 순서:

1. 실패한 테스트 이름, 종료 코드, 마지막 로그 구간을 즉시 기록한다.
2. 실패를 가장 작은 단위의 단일 `ctest -R '^name$'` 또는 대응 core 회귀로 다시 재현한다.
3. 원인을 `core/` 코드에서 분석하고 수정한다.
4. 실패한 단일 회귀를 먼저 통과시킨다.
5. 실패가 발생했던 원래 장시간 gate 전체를 처음부터 다시 실행해서 성공시킨다.
6. 원래 장시간 gate 성공 로그까지 확보한 뒤에만 5.0 표와 체크리스트를 갱신하고 다음 미완료 행으로 이동한다.

금지:

- 장시간 gate 실패를 알고도 다음 phase 구현으로 넘어가는 것
- 단일 재현 없이 timeout 값만 늘리거나 테스트 목록을 줄여서 gate를 우회하는 것
- 실패한 gate 대신 일부 하위 테스트 몇 개만 통과시킨 뒤 완료로 표시하는 것

증거 기록 규칙:

- 장시간 검증 로그는 `doc/plan/refactor/2nd/logs/` 아래에 남긴다.
- `사전 검증` 단계도 같은 콘솔/로그에 남겨서, 테스트 본 실행 전에 검증이 수행됐음을 로그로 증명한다.
- wrapper를 사용한 경우에도 최종 증거는 wrapper 로그가 아니라 내부 gate 명령의 실행 로그와 종료 코드 파일까지 추적 가능해야 한다.
- 장시간 검증은 시작 시각, 로그 경로, 필요하면 PID를 남긴다.
- `완료`로 바꿀 때는 최종 성공 로그 경로까지 표의 `검증 증거` 칸에 기록한다.
- 2026-03-24 재리뷰 기준으로, 현재 워크트리에 존재하지 않는 로그 경로를 근거로
  `완료` 상태를 유지하지 않는다.

### 5.0.2 단계 종료 commit/push 규칙

각 phase 또는 5.0 표의 각 행을 `완료`로 바꾸는 순간에는 아래 순서를 의무로
수행한다.

1. 해당 phase/행과 직접 연결된 코드, 테스트, 문서만 다시 확인한다.
2. `git status --short`로 현재 워크트리를 확인한다.
3. 이번 phase/행에서 만든 변경만 commit한다.
4. commit 직후 현재 작업 브랜치에 push한다.
5. push한 commit hash를 5.0 표의 `검증 증거` 또는 인접 진행 메모에 남긴다.

해석 규칙:

- phase 완료와 commit/push는 분리하지 않는다. phase를 `완료`로 올렸다면 같은
  흐름에서 commit/push까지 끝낸다.
- unrelated 변경이 워크트리에 섞여 있으면 그것까지 묶어서 commit하지 않는다.
- unrelated 변경 때문에 phase 범위만 안전하게 commit할 수 없으면 그 행은 아직
  `완료`가 아니다. 범위를 정리한 뒤 commit/push까지 끝내고 닫는다.
- 커밋 메시지는 phase/owner가 바로 드러나게 짧고 구체적으로 쓴다.
- push 대상은 현재 작업 브랜치다. 별도 release/tag 작업을 의미하지 않는다.
- 최종 완료 직전에도 동일 규칙을 적용하되, 마지막 gate 결과와 문서 갱신을 함께
  포함해 commit/push한다.

| 마스터 플랜 항목 | 상태 | 실행 문서 체크 항목 | 관련 코드/파일 | 검증 증거 |
|---|---|---|---|---|
| 갭 리뷰 문서 추가 및 실행 authority 반영 | 완료 | 5.0 재판정 | `doc/plan/refactor/2nd/core-system-posd-refactor-gap-review.ko.md`, `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md` | 갭 리뷰 문서 추가, 실행 문서 기준선/순서/추적표 갱신 |
| Residual 실행 스펙 추가 및 authority 반영 | 완료 | 5.0 재판정 | `doc/plan/refactor/2nd/core-system-posd-refactor-residual-execution-spec.ko.md`, `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md` | residual 실행 스펙 추가, `5.2A`/`5.3A`/`5.6A` 구현 경계/검증 명령 고정 |
| Phase 1a context/message/errno/version 분리 유지 | 완료 | 5.1 | `core/src/api/context_api.cpp`, `core/src/api/message_api.cpp` | `cmake --build core/build -j"$(nproc)"`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 1b socket/poller/monitor 분리 유지 | 완료 | 5.1 | `core/src/api/socket_*.cpp`, `core/src/api/poller_*.cpp`, `core/src/api/monitor*.cpp` | `ctest --test-dir core/build --output-on-failure -R '^(unittest_typed_option|unittest_poller)$'`, `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_monitor_service_contract|test_monitor_with_handler)$'` |
| Phase 1b.5 logical multipart send deep module 유지 | 완료 | 5.1, 5.6 | `core/src/core/multipart_send_txn.*`, gateway/spot publish/send caller | `ctest --test-dir core/build --output-on-failure -R '^test_gateway_handover$'`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'` |
| Phase 1c `zlink.cpp` concrete service knowledge 제거 완결 | 완료 | 5.1 | `core/src/api/zlink.cpp`, `core/src/api/service_*.cpp` | `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'` |
| Phase 2 `socket_base_t` semantic/runtime 분리 | 완료 | 5.2 | `core/src/sockets/socket_base*`, `core/src/sockets/socket_runtime.cpp` | `ctest --test-dir core/build --output-on-failure -R '^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'`, `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log`, `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log.exitcode` |
| Phase 3 close/drain contract 명확화 | 완료 | 5.3 | `core/src/services/common/service_runtime_base.*`, `core/src/services/common/service_socket_registry.hpp`, `core/src/core/ctx.*`, `core/src/sockets/socket_close_ops.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'`, `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log`, `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log.exitcode` |
| Phase 4 option ownership 분리 | 완료 | 5.4 | `core/src/core/options_dispatch.cpp`, `core/src/core/options_core_socket.cpp`, `core/src/core/options_transport_network.cpp`, `core/src/core/options_protocol_metadata.cpp`, `core/src/core/options_owner.*`, `core/src/api/zlink_option*.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^unittest_typed_option$'`, `ctest --test-dir core/build --output-on-failure -R '^test_stream_threadsafe$'`, `ctest --test-dir core/build --output-on-failure -R '^test_spot_service_introspection$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 5 service access/factory 경계 재정의 | 완료 | 5.5 | `core/src/api/service_*.cpp`, `core/src/api/monitor_service_open_api.cpp`, `core/src/services/*/*_access.*`, `core/src/services/spot/spot_subject_access.*`, `core/src/services/discovery/registry_query_access.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'` |
| Phase 6 gateway 세부 분해 | 완료 | 5.6 | `core/src/services/gateway/*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 6 discovery 세부 분해 | 완료 | 5.6 | `core/src/services/discovery/*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 6 spot 세부 분해 | 완료 | 5.6 | `core/src/services/spot/spot_data_plane.cpp`, `core/src/services/spot/spot_data_plane_protocol.cpp`, `core/src/services/spot/spot_data_plane_forwarding.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/services/spot/spot_sub*.cpp`, `core/src/services/spot/spot_subject_access.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'`, `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log`, `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 2~3 thread-safe stress 의무 게이트 | 완료 | 5.2, 5.3 | `core/tests/run_thread_safe_contract_stress.sh` | `Phase 2`: `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log`, `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log.exitcode`; `Phase 3`: `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log`, `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log.exitcode` |
| Phase 5~6 service core smoke | 완료 | 5.5, 5.6 | `core/src/api/service_*.cpp`, `core/src/services/*` | `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log`, `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log.exitcode` |
| Gap review 기준 `socket_base_t` residual split 재개 | 완료 | 5.2A | `core/src/sockets/socket_base.hpp`, `core/src/sockets/socket_base.cpp`, `core/src/sockets/socket_base_api.cpp`, `core/src/sockets/socket_base_dispatch.cpp`, `core/src/sockets/socket_base_endpoint.cpp`, `core/src/sockets/socket_base_lifecycle.cpp`, `core/src/sockets/socket_base_monitor.cpp`, `core/src/sockets/socket_runtime.hpp`, `core/src/sockets/socket_runtime.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'`, `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log`, `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers`, `commit: 6f236851` |
| Gap review 기준 `ctx_t` runtime orchestration residual split | 완료 | 5.3A | `core/src/core/ctx.hpp`, `core/src/core/ctx.cpp`, `core/src/core/ctx_bootstrap.*`, `core/src/core/ctx_termination.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'`, `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_215831.log`, `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_215831.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers`, `commit: e68d5e3d` |
| Gap review 기준 service residual deep-module 마감 | 완료 | 5.6A | `core/src/services/discovery/registry.cpp`, `core/src/services/discovery/registry_query.cpp`, `core/src/services/discovery/registry_state.cpp`, `core/src/services/discovery/registry_runtime.cpp`, `core/src/services/spot/spot_subject_access.cpp`, `core/src/services/spot/spot_subject_query.cpp`, `core/src/services/spot/spot_subject_poller.cpp`, `core/src/services/spot/spot_subject_publish.cpp`, `core/src/services/spot/spot_data_plane.cpp`, `core/src/services/spot/spot_data_plane_runtime.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|unittest_spot_subject_access|unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers`, `commit: efdc15db` |
| Post-residual 기준 `spot` secure multi-peer 구조 잔여 마감 | 완료 | 5.7A | `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/services/spot/spot_data_plane_protocol.cpp`, `core/src/services/spot/spot_data_plane_runtime.cpp`, `core/src/services/spot/spot_node.cpp`, `core/src/services/spot/spot_node_control.cpp`, `core/src/services/spot/spot_runtime.cpp`, `core/src/services/spot/spot_runtime.hpp`, `core/tests/unittest/unittest_spot_data_plane_budget.cpp`, `core/tests/unittest/unittest_spot_data_plane_protocol.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`, `doc/plan/refactor/2nd/logs/phase5_7A_spot_owner_gate_20260325_110639.log`, `doc/plan/refactor/2nd/logs/phase5_7A_spot_owner_gate_20260325_110639.log.exitcode`, `ctest --test-dir core/build --output-on-failure -R '^(test_spot_pubsub_scenario|test_spot_service_introspection)$'`, `doc/plan/refactor/2nd/logs/phase5_7A_spot_service_gate_20260325_110850.log`, `doc/plan/refactor/2nd/logs/phase5_7A_spot_service_gate_20260325_110850.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers`, `commit: 73ffa1b5` |
| Post-residual 기준 `socket_message_api.cpp` / `options_t` ownership 재정리 | 완료 | 5.7B | `core/src/api/socket_message_api.cpp`, `core/src/api/socket_message_send_api.cpp`, `core/src/api/socket_message_handler_api.cpp`, `core/src/core/options.hpp`, `core/src/core/options_owner.cpp`, `core/src/core/options_dispatch.cpp`, `core/CMakeLists.txt` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_typed_option|test_stream_threadsafe|test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'`, `doc/plan/refactor/2nd/logs/phase5_7B_message_option_gate_20260325_112021.log`, `doc/plan/refactor/2nd/logs/phase5_7B_message_option_gate_20260325_112021.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers`, `commit: d8dabbaf` |
| Post-residual 기준 `spot_node_t` handle/defaults/facade 구조 마감 | 완료 | 5.7C | `core/src/services/spot/spot_node.hpp`, `core/src/services/spot/spot_node_handles.cpp`, `core/src/services/spot/spot_node_lifecycle.cpp`, `core/src/services/spot/spot_internal_receiver.*`, `core/tests/e2e/spot/test_spot_service_introspection.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_spot_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection_handler_monitor_close)$'`, `doc/plan/refactor/2nd/logs/phase5_7C_spot_node_gate_20260325_113854.log`, `doc/plan/refactor/2nd/logs/phase5_7C_spot_node_gate_20260325_113854.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers`; owner 문장: `spot_node_t`는 semantic facade이고 default handle lifecycle/option defaults는 `spot_node_default_handles_t`가 소유; `commit: 6c3b2812` |
| Post-residual 기준 `discovery_t` bootstrap/uplink/facade 구조 마감 | 미착수 | 5.7D | `core/src/services/discovery/discovery.hpp`, `core/src/services/discovery/discovery_bootstrap.cpp`, `core/src/services/discovery/discovery_uplink.cpp`, `core/src/services/discovery/discovery_protocol.cpp` | owner 재평가 근거: `core-system-posd-refactor-post-residual-review.ko.md`; representative gate는 current owner 정리 후 기록 |
| Post-residual 기준 engine / transport owner 재판정 | 미착수 | 5.7E | `core/src/engine/asio/asio_engine.cpp`, `core/src/transports/ws/asio_ws_engine.cpp` | owner 재평가 근거: `core-system-posd-refactor-post-residual-review.ko.md`; representative gate는 current 구조 정리 후 기록 |

이 표는 각 작업 묶음이 끝날 때마다 반드시 갱신한다.
표에 `미착수`, `진행중`, `검증중`이 하나라도 남아 있으면 문서 완료가 아니다.
표의 `검증 증거` 칸이 비어 있으면 상태를 `완료`로 바꿀 수 없다.
5.0 표의 첫 미완료 행보다 아래 행을 먼저 진행하는 것은 금지한다.
`5.7A`~`5.7E`는 post-residual 구조 잔여를 순서대로 닫기 위한 행이다.
성능 개선용 별도 umbrella 행은 두지 않는다.

### 5.1 API facade 마무리

- [x] `core/src/api/zlink_option.cpp`를 common option facade / raw socket specialized option facade / raw subscription facade로 더 분리한다.
- [x] `core/src/api/monitor_api.cpp`와 `core/src/api/monitor_service_api.cpp`의 monitor query/decode/service-specific branching을 더 좁은 seam으로 내린다.
- [x] API 계층에서 concrete service knowledge가 다시 재집중되는 경로가 없는지 재점검한다.
- [x] `Phase 1c` 완료 기준인 대표 core API smoke를 수행한다.

닫힘 기준:

- [`zlink_option.cpp`](/home/hep7/project/kairos/zlink/core/src/api/zlink_option.cpp) 하나가 raw socket option + raw subscription + service bridge를 동시에 소유하지 않는다.
- monitor API 파일은 query/decode/service-specific wiring을 분리된 TU로 설명할 수 있다.
- `core/include/zlink.h` / `core/src/libzlink.vers` diff 없음
- 대표 core API smoke 통과

### 5.2 socket runtime 책임 정리

- [x] `core/src/sockets/socket_base.cpp`를 semantic facade 수준으로 더 줄인다.
- [x] bind/connect/send/recv/event emission과 lifecycle glue가 hidden collaborator로 내려가도록 정리한다.
- [x] `socket_base_t` 변경이 family별 파일 수정으로 쉽게 번지지 않는지 확인한다.
- [x] 대표 socket 회귀와 `test_gateway_monitor_snapshot_churn` 보조 회귀를 통과한다. 검증: `ctest --test-dir core/build --output-on-failure -R '^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'`
- [x] `Phase 2` 완료 기준인 thread-safe stress를 다시 수행한다. 로그: `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log`, exitcode: `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log.exitcode`

닫힘 기준:

- [`socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)가 semantic entrypoint 위주로 읽힌다.
- bind/connect/send/recv/event emission/lifecycle glue owner를 각 collaborator로 설명할 수 있다.
- 대표 socket 회귀와 thread-safe stress 통과

### 5.2A gap review 기준 `socket_base_t` residual split

- [x] `socket_runtime_t` aggregation이 다시 `socket_base_t` 참조 멤버 허브로 풀리지 않게 owner를 더 숨긴다.
- [x] public API admission / callback depth / deferred close를 한 lifecycle coordinator로 더 좁힌다.
- [x] monitor queue/thread, endpoint bookkeeping, async mailbox quiesce가 semantic entrypoint에서 직접 읽히지 않게 줄인다.
- [x] residual split 후에도 family 파일군(`dealer/router/xpub/xsub/stream`) 수정 없이 대표 회귀를 통과하는 경로를 확인한다.
- [x] `test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn`과 stress를 다시 통과시킨다. 로그: `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log`, exitcode: `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log.exitcode`

필수 검증 명령:

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

git diff -- core/include/zlink.h core/src/libzlink.vers
```

구현 경계와 새 private 파일 허용 범위는
`core-system-posd-refactor-residual-execution-spec.ko.md`의 `3.1`~`3.7`을 따른다.

닫힘 기준:

- [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)와 [`socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)가 더 이상 lifecycle/dispatch/monitor/endpoint 모든 세부를 한 눈에 직접 소유하는 허브로 읽히지 않는다.
- `socket_runtime_t`는 상태 묶음이 아니라 owner-hidden collaborator 집합으로 설명 가능하다.
- residual split 이후 대표 socket 회귀와 stress 로그가 새 증거로 남아 있다.

### 5.3 close/drain contract 정리

- [x] `service_runtime_base_t`, `service_socket_registry_t`, `ctx_t`, `socket_close_ops_t`의 의미 경계를 더 선명하게 만든다.
- [x] close/wait/drain 의미 owner를 코드 구조로 설명 가능하게 만든다.
- [x] lifecycle coordinator와 global removal tracking의 협력 지점을 더 명시적으로 드러낸다.
- [x] `Phase 3` 완료 기준인 thread-safe stress를 다시 수행한다. 로그: `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log`, exitcode: `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log.exitcode`

닫힘 기준:

- `service_runtime_base_t`는 lifecycle coordinator로, registry/close/wait는 collaborator contract로 설명된다.
- close/wait/drain 의미 owner를 `ctx_t`, registry, runtime으로 분리해 설명 가능하다.
- thread-safe stress 통과

### 5.3A gap review 기준 `ctx_t` runtime orchestration residual split

- [x] `ctx_t`에서 lazy start / thread runtime 부팅 / termination sequencing / pending inproc 정리 중 분리 가능한 owner를 다시 추출한다.
- [x] `service_control_runtime()`의 시작 보조와 `start()`/`terminate()` sequencing이 `ctx_t` 단일 허브 지식으로 남지 않게 줄인다.
- [x] global socket removal owner라는 핵심 책임은 유지하되, startup/shutdown/resource orchestration 지식을 더 숨긴다.
- [x] residual split 후 self-close / service lifecycle / drain 회귀를 다시 통과시킨다.

필수 검증 명령:

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

git diff -- core/include/zlink.h core/src/libzlink.vers
```

구현 경계와 새 private 파일 허용 범위는
`core-system-posd-refactor-residual-execution-spec.ko.md`의 `4.1`~`4.7`을 따른다.

닫힘 기준:

- [`ctx.cpp`](/home/hep7/project/kairos/zlink/core/src/core/ctx.cpp)가 startup/shutdown/resource finalization 세부를 모두 직접 조정하는 단일 허브로 읽히지 않는다.
- `ctx_t`의 핵심 책임을 "global runtime registry + termination contract owner" 수준으로 더 좁혀 설명할 수 있다.
- 대표 self-close / service lifecycle 회귀와 필요한 gate 로그가 새 증거로 남아 있다.

### 5.4 option ownership 마무리

- [x] `core/src/core/options_dispatch.cpp`를 owner별 validation/apply module로 더 쪼갠다.
- [x] owner map이 실제 handler 배치와 항상 일치하도록 유지한다.
- [x] `options_t` field는 유지하되, 누가 어떤 option을 해석하는지가 더 좁은 owner 경계로 설명되게 만든다.
- [x] option owner map을 문서와 코드 모두에 명시한다.

Phase 4에서 고정한 option owner map은 아래처럼 해석한다.

- `core-socket`
  - `sndhwm`, `rcvhwm`, `affinity`, `routing_id`, `ipv6`, `immediate`, `conflate`
  - `stream_notify`, `handshake_ivl`, `heartbeat_ivl`, `heartbeat_timeout`, `zmp_metadata`
- `transport-network`
  - `sndbuf`, `rcvbuf`, `linger`, `connect_timeout`, `tcp_maxrt`, `reconnect_*`
  - `tcp_keepalive*`, `tcp_nodelay`, `bindtodevice`, `tos`, `backlog`, `rcvtimeo`, `sndtimeo`
- `protocol-metadata`
  - `heartbeat_ttl`
  - `tls_cert`, `tls_key`, `tls_ca`, `tls_verify`, `tls_require_client_cert`, `tls_hostname`, `tls_trust_system`, `tls_password`
- `service-specific`
  - `subscribe`, `unsubscribe`, `topics_count`
  - router/xpub 계열 option, `FD`, `EVENTS`, `LAST_ENDPOINT`
  - central `options_t` bag이 직접 해석하지 않고 service/socket seam 뒤에서 처리

닫힘 기준:

- `getsockopt/setsockopt` round-trip 회귀 유지
- transport/TLS/routing/subscription/heartbeat 계열이 owner별 module로 설명된다.
- option owner map이 코드와 문서에 모두 존재한다.

### 5.5 service seam 정제

마스터 플랜 참조:
[6.6 Phase 5: service access / factory 경계 재정의](./core-system-posd-refactor-master-plan.ko.md) (약 1058행),
[7.5 Service 계층 규칙](./core-system-posd-refactor-master-plan.ko.md) (약 1204행)

- [x] `service_*_api.cpp` 계열이 새로운 허브가 되지 않도록 role별로 더 나눈다.
- [x] `gateway`/`discovery`의 create/status/destroy/monitor open 경로는 concrete type 검증을 API에 남기지 않고 service-local access seam으로 읽히게 유지한다.
- [x] `spot_node`의 service monitor open 경로는 pub/internal receiver 선택을 API에서 직접 조합하지 않고 service-local access seam을 통해 연다.
- [x] create/attach/start/stop/query/monitor/poller가 일관된 `API -> seam -> concrete service` 구조로 읽히게 만든다.
- [x] recv/send-ready handler registration 경로도 일관된 `API -> seam -> concrete service` 구조로 읽히게 만든다.
- [x] seam이 concrete branching 집합이 아니라 실제 deep module 역할을 하도록 정리한다.
- [x] `service_public_api_guard_t`는 service 의미 API가 아니라 public admission/close guard로만 남기고, service 의미 surface는 service-local seam이 소유하게 만든다.
- [x] `services/common/service_access/*` 같은 cross-service access hub를 만들지 않고 service-local seam + common guard 방향을 유지한다.
- [x] service concrete type 변경이 public API 계층의 direct include 변경으로 바로 이어지지 않도록 남은 direct concrete dependency를 줄인다.

닫힘 기준:

- `service_*_api.cpp` 계열이 새 `zlink.cpp` 역할을 하지 않는다.
- create/attach/start/stop/query/handler/monitor/poller 대표 경로를 file-level owner로 설명 가능하다.
- service concrete type 변경이 public API 계층의 direct include 변경으로 이어지지 않는다.
- `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection` 유지

### 5.6 서비스별 deep module 완료

마스터 플랜 참조:
[6.7 Phase 6: 서비스별 세부 분해](./core-system-posd-refactor-master-plan.ko.md) (약 1093행),
[7.5 Service 계층 규칙](./core-system-posd-refactor-master-plan.ko.md) (약 1204행),
[7.4 Option 계층 규칙](./core-system-posd-refactor-master-plan.ko.md) (약 1188행)

- [x] `gateway`에서 lifecycle / topology refresh / monitor / socket facade / TLS/routing-id attach/query / data-plane 경계를 더 명확히 고정한다.
- [x] `discovery`에서 bootstrap / uplink / registry client / update / state owner를 더 선명히 고정한다.
- [x] `discovery` protocol encode/decode owner가 runtime orchestration과 섞이지 않도록 discovery-local 경계로 고정한다.
- [x] `spot_node`에서 node orchestration / handle composition / discovery-aware control을 더 줄인다.
- [x] `spot_sub`에서 lifecycle / subject state / recv/direct-handler / option owner를 더 줄인다.
- [x] `spot_data_plane.cpp`를 protocol/data-plane/assembly 경계로 더 쪼갠다.
- [x] `spot` internal receiver/dispatch 경로가 node orchestration이나 data-plane 허브에 다시 흡수되지 않도록 service-local seam으로 유지한다.

닫힘 기준:

- `gateway`, `discovery`, `spot` 각각에 대해 lifecycle / protocol / topology / data-plane owner를 분리해 설명할 수 있다.
- `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection`, `test_spot_pubsub_scenario`, `test_spot_service_introspection`, `test_monitor_service_contract` 유지
- `spot_data_plane.cpp`가 단일 허브처럼 읽히지 않는다

### 5.6A gap review 기준 service residual deep-module 마감

- [x] `registry.cpp`에서 registry state/rules, socket ensure/replace, topology query/reply owner를 더 좁은 경계로 다시 나눈다.
- [x] `spot_subject_access.cpp`에서 publish/query/poller/subject access seam이 다시 허브로 커지지 않게 owner를 더 분리한다.
- [x] `spot_data_plane.cpp`에서 runtime assembly, forwarding, budget/control carryover owner를 더 선명하게 고정한다.
- [x] residual service 정리 뒤 full service core smoke와 대표 회귀를 다시 통과시킨다.

필수 검증 명령:

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'

ctest --test-dir core/build --output-on-failure -R \
'^(unittest_service_mode_policy|unittest_spot_subject_access|unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'

git diff -- core/include/zlink.h core/src/libzlink.vers
```

구현 경계와 새 private 파일 허용 범위는
`core-system-posd-refactor-residual-execution-spec.ko.md`의 `5.1`~`5.6`을 따른다.

닫힘 기준:

- [`registry.cpp`](/home/hep7/project/kairos/zlink/core/src/services/discovery/registry.cpp), [`spot_subject_access.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_subject_access.cpp), [`spot_data_plane.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane.cpp)가 각각 단일 허브보다 owner-separated module 집합으로 읽힌다.
- `gateway`, `discovery`, `spot` 각각의 잔여 large file owner를 문장 하나로 설명할 수 있다.
- residual service 정리 이후 full service core smoke와 대표 회귀 로그가 새 증거로 남아 있다.

### 5.7A post-residual 기준 `spot` secure multi-peer 구조 잔여 마감

참고 authority:

- [post-residual-review `3. 실행 로그 기준 현재 active issue`](./core-system-posd-refactor-post-residual-review.ko.md#3-실행-로그-기준-현재-active-issue)
- [post-residual-review `10. 함께 닫아야 하는 구조 범위`](./core-system-posd-refactor-post-residual-review.ko.md#10-현재-perf-마감을-위해-함께-닫아야-하는-구조-범위)
- [residual-execution-spec `9. 현재 active handoff`](./core-system-posd-refactor-residual-execution-spec.ko.md#9-현재-active-handoff)

구현 고정 해석:

- top-level owner 문장: `spot_node_t`는 service semantic facade이고, secure multi-peer hot-path detail은 `runtime/protocol/control` private owner가 가진다.
- 남길 책임: service semantic entry, external lifecycle contract, public handler/summary facade
- 숨길 책임: monitor ready 해석, connected peer membership, ready peer count, budget/control hint, secure send path detail
- 허용 파일 경계: `spot_data_plane_internal.hpp`, `spot_data_plane_protocol.cpp`, `spot_data_plane_runtime.cpp`, `spot_node_control.cpp`, `spot_runtime.hpp`, 필요 시 이에 직접 연결된 `core/tests/` 회귀
- 금지: `spot_node_t`에 hot-path field/cache를 다시 추가하는 것, timing 완화로 회귀를 숨기는 것, `core/perf/`를 수정하는 것
- 필수 대표 gate: `unittest_spot_data_plane_budget`, `unittest_spot_data_plane_protocol`, `test_single_spot_benchmark_process`, `test_multi_spot_benchmark_process`, `test_spot_pubsub_scenario`, `test_spot_service_introspection`

- [x] `spot` secure multi-peer 경로에서 남아 있는 lifecycle/control/data-plane 허브를 다시 기록한다.
- [x] [`spot_data_plane_internal.hpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane_internal.hpp),
  [`spot_data_plane_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane_runtime.cpp),
  [`spot_node_control.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node_control.cpp),
  [`spot_runtime.hpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_runtime.hpp)
  사이의 ownership/invariant를 다시 한 문장씩 고정한다.
- [x] monitor ready 해석, connected peer 상태, budget/control hint, secure send path가
  각각 어느 private owner에 속하는지 다시 분리한다.
- [x] `spot_node_t` top-level facade가 hot path detail을 과하게 아는 지점을 더 줄인다.
- [x] 필요 시 `core/tests` 회귀를 보강하되 timing/perf 현상을 숨기는 완화가 아니라
  구조 owner를 더 좁히는 재현 surface로만 추가한다.
- [x] 변경 후 아래 검증을 직렬로 다시 닫는다.
  - `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_spot_pubsub_scenario|test_spot_service_introspection)$'`
  - `git diff -- core/include/zlink.h core/src/libzlink.vers`

구조 평결:

- `spot_data_plane_internal.hpp`는 secure multi-peer monitor membership, ready-peer count, budget version invariant를 숨기는 private state owner다.
- `spot_data_plane_protocol.cpp`는 monitor/control decode를 `mesh_peer_state` 갱신과 control wakeup으로 연결하는 protocol owner다.
- `spot_data_plane_runtime.cpp`는 live `mesh_pub` budget apply와 runtime socket wiring을 소유하고, semantic facade에 live socket tuning detail을 노출하지 않는다.
- `spot_node_control.cpp`는 semantic facade로 남아 peer-version 관찰과 subscription/pub delivery hint만 조정한다.
- `spot_runtime.hpp`는 runtime socket bundle과 `mesh_peer_state` storage를 들고, hot-path cache 정책은 직접 소유하지 않는다.

검증 메모:

- 최초 representative gate에서 `test_multi_spot_benchmark_process`의 `tls 262144` 경로가 한 차례 timeout을 보였고, 단일 재현 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`는 통과했다.
- 이후 explicit disconnect가 `mesh_peer_state` 버전/wakeup 불변식을 빠뜨리지 않도록 helper를 추가하고, 신규 unit 회귀 `test_explicit_disconnect_updates_private_mesh_peer_state`를 보강한 뒤 representative gate를 새 로그로 다시 통과시켰다.

닫힘 기준:

- `spot` secure multi-peer 경로를 lifecycle / protocol / runtime / control owner로 나눠 설명할 수 있다.
- `spot_node_t`는 semantic facade로, hot path detail은 private owner로 더 줄어든다.
- representative `core/tests` 회귀는 green이고 ABI surface diff가 없다.

### 5.7B post-residual 기준 `socket_message_api.cpp` / `options_t` ownership 재정리

참고 authority:

- [post-residual-review `8. 코드 전반 우선순위 재판정`](./core-system-posd-refactor-post-residual-review.ko.md#8-코드-전반-우선순위-재판정)
- [post-residual-review `10. 함께 닫아야 하는 구조 범위`](./core-system-posd-refactor-post-residual-review.ko.md#10-현재-perf-마감을-위해-함께-닫아야-하는-구조-범위)
- [residual-execution-spec `6. post-residual 진입 및 재진입 규칙`](./core-system-posd-refactor-residual-execution-spec.ko.md#6-post-residual-진입-및-재진입-규칙)

구현 고정 해석:

- top-level owner 문장: `socket_message_api.cpp`는 public message facade이고, domain-specific decode/dispatch는 narrower entry owner로 내려가야 한다.
- 남길 책임: 공개 message API entry, 공통 tag/argument 검증, semantic routing to collaborator
- 숨길 책임: socket/service/stream/spot별 concrete branching, option bag 직접 해석, domain-specific reply/decode detail
- 허용 파일 경계: `socket_message_api.cpp`, `options.hpp`, `options_owner.cpp`, `options_dispatch.cpp`, 필요 시 이에 직접 연결된 `core/src/api/` private helper와 `core/tests/`
- 금지: public API surface 변경, unrelated service/transport 파일로 확산, `options_t`를 다른 central bag 이름으로만 바꾸는 이동
- 필수 대표 gate: `unittest_typed_option`, `test_stream_threadsafe`, `test_spot_service_introspection`, 필요 시 `Phase 1c` 대표 core API smoke 일부 재실행

- [x] [`socket_message_api.cpp`](/home/hep7/project/kairos/zlink/core/src/api/socket_message_api.cpp)가
  socket/service/stream/spot 분기 허브로 남는 지점을 다시 기록한다.
- [x] [`options.hpp`](/home/hep7/project/kairos/zlink/core/src/core/options.hpp),
  [`options_owner.cpp`](/home/hep7/project/kairos/zlink/core/src/core/options_owner.cpp),
  [`options_dispatch.cpp`](/home/hep7/project/kairos/zlink/core/src/core/options_dispatch.cpp)가
  중앙 bag/dispatcher로 남는지 판정한다.
- [x] 공통 검증과 도메인별 entry를 분리하는 최소 경계를 먼저 문장으로 고정한다.
- [x] 이 항목은 `5.7A`가 닫히기 전에는 착수하지 않는다.

구조 평결:

- `socket_message_api.cpp`는 recv/xpub-subscription entry만 남는 public message facade로 줄였고, socket/service fallback 허브 역할은 더 이상 직접 소유하지 않는다.
- `socket_message_send_api.cpp`는 raw socket send/publish/routed-send entry와 whole-message send helper를 소유한다.
- `socket_message_handler_api.cpp`는 recv/send-ready handler registration과 poller admission guard를 소유한다.
- `options_owner.cpp`와 `options_dispatch.cpp`는 기존 owner map/dispatcher를 유지하되, 이번 항목의 immediate giant hub는 `socket_message_api.cpp` 쪽이라는 점을 재확인했다.

검증 메모:

- `5.7B`는 `socket_message_api.cpp` giant hub를 `recv entry / send entry / handler entry` 세 축으로 나누는 것을 완료 기준으로 삼았고, `options_t` storage layout과 owner map은 Phase 4 계약을 유지한 채 재판정만 수행했다.
- 마스터 플랜 Phase 1~6, 8.1, 8.2, 9 재대조 결과 이번 단계에서 실행 가이드에 추가해야 할 누락 구현 항목은 발견되지 않았다.

닫힘 기준:

- `socket_message_api.cpp`와 `options_t` 중 적어도 하나는 giant hub 설명에서 벗어난다.
- 변경 범위가 unrelated service/transport로 넓게 번지지 않는다.
- 대표 회귀와 ABI 무변경 확인을 함께 남긴다.

### 5.7C post-residual 기준 `spot_node_t` handle/defaults/facade 구조 마감

참고 authority:

- [post-residual-review `8. 코드 전반 우선순위 재판정`](./core-system-posd-refactor-post-residual-review.ko.md#8-코드-전반-우선순위-재판정)
- [post-residual-review `10. 함께 닫아야 하는 구조 범위`](./core-system-posd-refactor-post-residual-review.ko.md#10-현재-perf-마감을-위해-함께-닫아야-하는-구조-범위)
- [residual-execution-spec `6. post-residual 진입 및 재진입 규칙`](./core-system-posd-refactor-residual-execution-spec.ko.md#6-post-residual-진입-및-재진입-규칙)

구현 고정 해석:

- top-level owner 문장: `spot_node_t`는 service semantic facade이고, default handle lifecycle과 option default policy는 dedicated private owner가 가진다.
- 남길 책임: bind/connect/discovery attach 같은 semantic service entry, public child-handle contract, high-level snapshot facade
- 숨길 책임: default `pub/sub` 생성/보존/파기, internal receiver 생성/전환, option default 저장/적용, fast-path cache detail
- 허용 파일 경계: `spot_node.hpp`, `spot_node_handles.cpp`, `spot_node_control.cpp`, `spot_internal_receiver.*`, 필요 시 이에 직접 연결된 `test_spot_service_introspection`
- 금지: `spot_node.hpp`에 friend/access seam을 더 늘리는 것, handle lifecycle을 `spot_node_control.cpp`로 다시 되밀어 넣는 것, public child-handle contract 변경
- 필수 대표 gate: `test_spot_service_introspection`, `test_spot_pubsub_scenario`, `test_spot_service_introspection_handler_monitor_close`, `git diff -- core/include/zlink.h core/src/libzlink.vers`

- [x] [`spot_node.hpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node.hpp)와
  [`spot_node_handles.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node_handles.cpp)가
  semantic facade인지 default-handle coordinator인지 다시 판정한다.
- [x] default `pub/sub`, internal receiver, option default 저장/적용이 어떤 private owner에 속하는지 먼저 문장으로 고정한다.
- [x] `spot_node_control.cpp` current owner 수정이 끝난 뒤에도 top-level facade가 hot-path와 handle lifecycle을 함께 오염시키는지 확인한다.
- [x] friend/access seam과 fast-path pointer cache가 내부 우회 통로로 남는 지점을 줄인다.
- [x] 이 항목은 `5.7A`, `5.7B` 완료 후에만 진행한다.

닫힘 기준:

- `spot_node_t` 설명이 "semantic facade + handle/defaults owner 위임" 수준으로 줄어든다.
- default handle 생성/보존/파기와 option default 적용이 top-level facade 바깥 private owner로 설명된다.
- service core 회귀가 유지된다.

### 5.7D post-residual 기준 `discovery_t` bootstrap/uplink/facade 구조 마감

참고 authority:

- [post-residual-review `8. 코드 전반 우선순위 재판정`](./core-system-posd-refactor-post-residual-review.ko.md#8-코드-전반-우선순위-재판정)
- [post-residual-review `10. 함께 닫아야 하는 구조 범위`](./core-system-posd-refactor-post-residual-review.ko.md#10-현재-perf-마감을-위해-함께-닫아야-하는-구조-범위)
- [residual-execution-spec `6. post-residual 진입 및 재진입 규칙`](./core-system-posd-refactor-residual-execution-spec.ko.md#6-post-residual-진입-및-재진입-규칙)

구현 고정 해석:

- top-level owner 문장: `discovery_t`는 discovery semantic facade이고, registry bootstrap/uplink/control detail은 dedicated private owner가 가진다.
- 남길 책임: registry connect/register/update/unregister semantic contract, observer facade, topology snapshot facade
- 숨길 책임: bootstrap retry/wakeup, routing-id lock detail, socket option 저장/적용, tls client option materialization, uplink/control dealer state
- 허용 파일 경계: `discovery.hpp`, `discovery_bootstrap.cpp`, `discovery_uplink.cpp`, `discovery_protocol.cpp`, 필요 시 이에 직접 연결된 `core/tests/`
- 금지: bootstrap state를 `discovery_t` 본체 field로 더 늘리는 것, `spot/gateway`에서 discovery internal state를 직접 읽게 하는 것, transport restriction contract 완화
- 필수 대표 gate: `test_service_discovery`, `test_service_introspection`, `test_service_introspection_discovery_self_close`, `test_service_introspection_discovery_control_path`, `test_monitor_service_contract`

- [ ] [`discovery.hpp`](/home/hep7/project/kairos/zlink/core/src/services/discovery/discovery.hpp)와
  [`discovery_bootstrap.cpp`](/home/hep7/project/kairos/zlink/core/src/services/discovery/discovery_bootstrap.cpp),
  [`discovery_uplink.cpp`](/home/hep7/project/kairos/zlink/core/src/services/discovery/discovery_uplink.cpp)가
  semantic facade인지 bootstrap/uplink coordinator인지 다시 판정한다.
- [ ] registry bootstrap, routing-id lock, socket option 저장/적용, tls client option 반영이 어떤 private owner에 속하는지 먼저 문장으로 고정한다.
- [ ] bootstrap retry/wakeup/control task 시작 조건이 top-level facade와 뒤섞인 지점을 줄인다.
- [ ] friend/access seam과 internal bootstrap helper가 상태 우회 통로로 남는 지점을 줄인다.
- [ ] 이 항목은 `5.7A`, `5.7B`, `5.7C` 완료 후에만 진행한다.

닫힘 기준:

- `discovery_t` 설명이 "semantic facade + bootstrap/uplink owner 위임" 수준으로 줄어든다.
- registry bootstrap과 uplink/control state가 top-level facade 바깥 private owner로 설명된다.
- service/discovery 대표 회귀가 유지된다.

### 5.7E post-residual 기준 engine / transport owner 재판정

참고 authority:

- [post-residual-review `8. 코드 전반 우선순위 재판정`](./core-system-posd-refactor-post-residual-review.ko.md#8-코드-전반-우선순위-재판정)
- [post-residual-review `10.3 최종 구조 완료 판정 기준`](./core-system-posd-refactor-post-residual-review.ko.md#103-최종-구조-완료-판정-기준)
- [residual-execution-spec `8. 반복 리뷰 절차`](./core-system-posd-refactor-residual-execution-spec.ko.md#8-반복-리뷰-절차)

구현 고정 해석:

- top-level owner 문장: engine/transport는 current active 구조 row의 collaborator여야 하며, 독립 giant owner가 아니면 새 구조 작업을 열지 않는다.
- 남길 책임: backend-specific event loop, transport-local wire/channel execution
- 숨길 책임: service/socket semantic policy, cross-layer lifecycle orchestration, unrelated global owner knowledge
- 허용 파일 경계: `asio_engine.cpp`, `asio_ws_engine.cpp`, 해당 판정에 직접 필요한 companion file만 제한적으로 포함
- 금지: 단순 파일 크기만으로 분해를 시작하는 것, `5.7A`~`5.7D` 미해결 owner를 engine/transport로 떠넘기는 것, representative gate 없이 local polish를 완료로 적는 것
- 필수 대표 gate: 현재 판정과 직접 연결된 transport smoke 1개 이상 + `git diff -- core/include/zlink.h core/src/libzlink.vers`

- [ ] [`asio_engine.cpp`](/home/hep7/project/kairos/zlink/core/src/engine/asio/asio_engine.cpp)와
  [`asio_ws_engine.cpp`](/home/hep7/project/kairos/zlink/core/src/transports/ws/asio_ws_engine.cpp)가
  단순 대형 파일인지 실제 owner 붕괴인지 구분한다.
- [ ] 현재 구조 허브와 직접 연결되는 engine/transport owner가 있을 때만 구조 작업으로 승격한다.
- [ ] 현재 active 구조 row와 직접 연결되지 않으면 이 항목은 local polish 후보로 내린다.
- [ ] 이 항목은 `5.7A`, `5.7B`, `5.7C`, `5.7D` 이후 마지막 구조 판정으로만 수행한다.
- [ ] 실제 구조 작업이 불필요하다고 판정한 경우에도 그 근거와 representative 확인 명령을
  `5.0` 표의 검증 증거 칸에 남긴다.

닫힘 기준:

- engine/transport가 현재 active 구조 owner와 직접 연결되지 않으면 추가 구조 작업 없이 종료 판정한다.
- 직접 연결된다면 owner 하나와 representative gate 하나로 설명 가능해야 한다.
## 6. 검증 게이트

각 단계는 아래 게이트를 통과해야 다음으로 넘어간다.

### 6.1 공통 게이트

- `cmake --build core/build -j"$(nproc)"`
- 해당 변경에 직접 연결된 `unittest` / `integration` / `e2e` 회귀 묶음
- `core/include/zlink.h` diff 없음 확인
- `core/src/libzlink.vers` diff 없음 확인

### 6.2 phase별 추가 게이트

- `Phase 1c`: 대표 core API smoke
- `Phase 2`: `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10` 이상
- `Phase 3`: `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10` 이상
- `Phase 5~6`: full service core smoke
- `Gap review residual`: `5.2A`, `5.3A`, `5.6A` 각 항목의 필수 게이트를 residual execution spec 그대로 수행
- 최종 완료 직전: `./core/tests/run_test_lanes.sh --include-e2e`

### 6.3 최종 완료 게이트

아래를 모두 만족해야 "문서 완료"로 본다.

- 마스터 플랜 커버리지 추적 표의 미완료 상태가 0개
- 본 문서 5장의 체크리스트가 전부 체크됨
- `./core/tests/run_test_lanes.sh --include-e2e` 통과
- `Phase 1c` 대표 core API smoke 완료
- `Phase 5~6` full service core smoke 완료
- `Phase 2`, `Phase 3` 의무 stress 완료
- gap review 기준 residual 구조 항목(`5.2A`, `5.3A`, `5.6A`) 완료
- post-residual 추가 구조 항목(`5.7A`, `5.7B`, `5.7C`, `5.7D`, `5.7E`) 완료
- ABI surface 무변경 확인
- 남은 허브가 문서의 original concern 묶음을 다시 재집중시키지 않음

## 7. 작업 순서 고정

순서는 아래로 고정한다.

1. `zlink_option.cpp` / monitor API 정리
2. `socket_base.cpp` 정리
3. `service_runtime_base_t` 경계 정리
4. `options_dispatch.cpp` owner별 분리 마무리
5. service seam 정제
6. `gateway` / `discovery` 남은 deep module 마감
7. `spot_node` / `spot_sub` / `spot_data_plane` deep module 정리
8. gap review 기준 `socket_base_t` residual split
9. gap review 기준 `ctx_t` residual split
10. gap review 기준 `registry` / `spot` residual deep module 마감
11. post-residual 기준 `spot` secure multi-peer 구조 잔여 마감
12. post-residual 기준 `socket_message_api.cpp` / `options_t` ownership 재정리
13. post-residual 기준 `spot_node_t` handle/defaults/facade 구조 마감
14. post-residual 기준 `discovery_t` bootstrap/uplink/facade 구조 마감
15. post-residual 기준 engine / transport owner 재판정
16. stress / lane 최종 게이트
17. 최종 검증과 문서 완료 판정

이 순서를 어기려면 아래 둘 중 하나를 만족해야 한다.

- 현재 앞선 단계가 이미 사실상 닫혀 있고 다음 단계가 더 낮은 위험인 경우
- 테스트 회귀를 막는 직접 원인이 뒤 단계 파일에 있고, 그 수정이 앞 단계 설계를 훼손하지 않는 경우

각 단계 종료 직전에는 아래 질문에 모두 `예`로 답할 수 있어야 한다.

1. 이 단계의 대표 owner를 한 문장으로 설명할 수 있는가?
2. 이 단계의 대표 테스트 묶음이 green인가?
3. 이 단계 변경이 unrelated module 수정으로 넓게 번지지 않는가?
4. 다음 단계가 이전 단계의 의미를 다시 뒤흔들지 않는가?

## 7.3 post-residual 구현 기록 형식

`5.7A`~`5.7E`의 각 row는 구현 전에 아래 다섯 줄을 먼저 기록한 뒤 시작한다.

1. top-level owner 한 문장
2. 남길 책임 2~4개
3. 숨길 책임 2~4개
4. 이번 row의 필수 대표 gate
5. 이번 row의 금지 사항 중 특히 위험한 것 1개

구현이 끝난 뒤에는 아래 세 줄을 같은 row 증거에 남긴다.

1. 실제 수정 파일 목록
2. 통과한 gate 명령
3. "이제 이 row를 한 문장으로 어떻게 설명하는가" 최종 문장

post-residual 구조 단계의 반복 규칙은 아래처럼 고정한다.

1. `5.0` 표에서 가장 앞선 미완료 row 하나를 고른다.
2. 그 row의 대표 owner, invariant, representative gate를 한 문장씩 먼저 적는다.
3. `core/` 내부에서 허브를 줄이고 private owner를 깊게 만드는 refactor만 수행한다.
4. 필요한 경우에만 `core/tests/` 회귀를 보강하고, 테스트 의미를 약화시키지 않는다.
5. representative gate와 ABI 무변경 확인이 green이면 `5.0` 표 상태를 올리고 즉시 다음 row로 넘어간다.
6. gate가 red이면 같은 row 안에서만 수정하고, 다른 row로 점프하지 않는다.
7. 모든 row가 닫힐 때까지 이 순서를 반복한다.

post-residual 구조 단계에서의 금지 규칙은 아래와 같다.

- file split만으로 완료를 주장하는 것
- 테스트 의미를 약화시키거나 flake를 숨기기 위해 timeout/완화만 올리는 것
- representative gate가 red인데 다른 row로 넘어가는 것
- owner 설명 없이 구현부터 넓게 번지는 것
- 성능 회복 루프를 active 완료 기준으로 다시 끌어오는 것

## 7.1 반복 마스터 플랜 점검 절차

각 작업 묶음이 끝날 때마다 아래 절차를 반드시 수행한다.

1. `core-system-posd-refactor-master-plan.ko.md`의 Phase 1~6, 8.1, 8.2, 9장과 `core-system-posd-refactor-gap-review.ko.md`를 다시 훑는다.
2. 이번 작업으로 닫힌 항목이 있으면 5.0 표의 상태를 올린다.
3. 5.0 표에 아직 `미착수`, `진행중`, `검증중`이 남아 있으면 그중 최상단 항목을 다음 작업으로 고른다.
4. master plan에 있는데 5장 체크리스트에 없는 항목을 발견하면 즉시 5장에 새 체크 항목으로 추가한다.
5. master plan과 실행 문서가 어긋나면 실행 문서를 먼저 고치고 그 다음 코드를 진행한다.

이 절차는 "미완료 항목이 없는지 반복해서 확인"하는 공식 루프다.

## 7.2 미완료 0개 판정 규칙

아래 셋을 모두 만족해야 `미완료 0개`로 판정한다.

1. 5.0 추적 표에 `미착수`, `진행중`, `검증중`이 없다.
2. 5.1~5.7 체크리스트에 빈 칸이 없다.
3. 6.3 최종 완료 게이트가 모두 충족된다.

셋 중 하나라도 만족하지 못하면, 문서상 완료가 아니다.

## 8. 완료 보고 규칙

완료 보고는 아래 둘 중 하나일 때만 한다.

- 모든 체크리스트가 끝났을 때
- 3장의 예외 조건 중 하나가 발생했을 때

그 외에는 상태 공유를 위해 멈추지 않는다.

완료가 아니면 아래 이유로 보고하지 않는다.

- 줄 수 감소
- 파일 분리 자체
- 일부 테스트만 통과
- 특정 허브만 국소적으로 줄어든 상태

## 9. 유지 규칙

이 문서는 실행 문서다.
남은 항목이 줄어들면 체크 상태와 기준선을 같이 갱신한다.
다만 문서를 업데이트하는 행위가 실제 구현보다 앞서면 안 된다.

항상 코드와 테스트가 먼저다.
