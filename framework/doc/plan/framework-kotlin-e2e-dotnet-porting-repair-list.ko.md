# Kotlin Framework E2E .NET 기준 재포팅 수정 목록

## 목적

이 문서는 Kotlin framework E2E를 `framework/languages/dotnet/e2e` 기준으로 다시 대조했을 때, 포팅이
아니라 다른 구조로 구현된 항목을 수정 계획으로 정리한다.

## 공통 기준

1. `.NET` 기준 source-only inventory를 먼저 만든다. `bin`, `obj`, `logs` 산출물은 기준 role에서 제외한다.
2. Client는 HTTP client 또는 stream connector driver여야 한다. Framework application, spot, actor,
   registry participant로 직접 구동되면 수정 대상이다.
3. Server role은 `.NET`의 role/process 의미와 맞춘다. Extra role은 근거를 확인하고 없으면 제거하거나
   gap으로 분리한다.
4. Scenario 파일은 `.NET Client/Scenarios`와 공통 E2E scenario ID에 맞춘다.
5. public API가 없으면 내부 helper, raw frame, 테스트 전용 adapter로 우회하지 않는다.

## Framework 기능 누락과 버그 처리 원칙

누락된 Kotlin E2E 기능을 구현하는 중 framework 자체의 public 기능이 없거나 framework 버그가 드러나면,
E2E 코드에서 우회하지 않는다. 먼저 원인을 확인하고, 필요한 framework 기능을 같은 public contract 기준으로
추가하거나 framework 버그를 수정한다.

완료 조건은 다음을 모두 포함한다.

1. 문제 원인이 E2E harness, Kotlin binding, framework runtime, public API 중 어디에 있는지 확인한다.
2. framework 기능 누락이면 spec, 공통 framework 문서, 기존 public API 근거를 확인한 뒤 같은 수준의
   public 기능으로 추가한다.
3. framework 버그이면 실패를 재현하는 회귀테스트를 먼저 추가하거나, 같은 변경 안에서 테스트와 수정을 함께
   남긴다.
4. E2E code에 raw frame, private helper, test-only adapter, extra sleep, retry-only workaround를 넣어
   통과시키지 않는다.
5. 수정 뒤에는 framework 회귀테스트와 해당 E2E runner를 함께 실행하고 결과를 문서에 반영한다.

## 공통 local E2E 대기 기준

각 config를 `.NET` 기준으로 다시 맞출 때 runner의 local 대기 기준도 함께 정리한다. 대기값을 늘려서
실패를 덮지 않고, 정해진 기준 안에서 실패하면 harness 또는 framework 버그로 원인을 확인한다.

- local process readiness는 기본 3초, poll 간격은 0.1초로 둔다.
- route 전파 안정화가 필요한 구간은 `ROUTE_SETTLE_SECONDS` 같은 이름 있는 값으로 분리하고, 현재 공통
  기준은 5초로 맞춘다.
- scenario marker settle은 3초 기준으로 분리한다.
- HTTP readiness, admin, evidence probe는 3초 기준으로 맞추고, loop 안에서 probe timeout이 누적되어
  전체 local readiness가 3초를 넘지 않게 한다.
- shutdown, child process, full sweep처럼 구조적으로 긴 대기가 필요한 경우에는 별도 상수로 이름을 붙인다.
- 기본 검증 경로는 환경 변수 override나 retry-only workaround에 의존하지 않는다. 실패가 반복되면 로그와
  scenario evidence로 원인을 확인한다.

## 수정 목록

### 1. `RegistryMessaging`

현재 문제:

- `.NET` runner는 `backpressure-consumer` process를 따로 시작하고 Client option도
  `--backpressure-consumer-url`을 받는다.
- `.NET` Client는 `RM-C9`를 `backpressureConsumer` 대상으로 실행한다.
- Kotlin runner도 `backpressure-consumer`를 시작하고 Client option에 backpressure consumer URL을 넘긴다.
- Kotlin Client는 `RM-C9`를 backpressure consumer 대상으로 실행한다.

수정 방향:

- Kotlin runner에 `.NET` 기준 `backpressure-consumer` role/process를 추가했다.
- Client option에 backpressure consumer URL을 추가하고, `RM-C9`는 해당 endpoint를 대상으로 실행한다.
- public low-HWM 또는 backpressure 제어 API가 없어 같은 동작을 구현할 수 없으면 feature-map에 public
  contract gap으로 남기고 `singleConsumer` 대체 경로를 완료로 표시하지 않는다.

### 2. `RegistrationCodec`

현재 문제:

- Kotlin Client가 `@EnableZLinkFramework`, `ZLinkFrameworkConfigurer`, `ZLinkClient` 기반이다.
- `.NET` 기준 HTTP client driver와 실행 책임이 다르다.

수정 방향:

- Client를 HTTP client driver로 바꾼다.
- codec peer, invalid duplicate, requester 책임은 server role로 옮긴다.
- `ClientScenario.kt`는 HTTP endpoint 호출 scenario로 다시 나눈다.
- scenario ID 구현이 아닌 helper는 `Client/Scenarios`에서 제거하거나 `Client/Support`로 옮긴다.

### 3. `ResilienceLifecycle`

현재 문제:

- Kotlin Client가 `@EnableZLinkFramework`로 구동되고 `ZLinkClient`를 직접 사용한다.
- `.NET` 기준 `Server/Consumer` role이 없다.
- `RL-B2`, `RL-D2`, `RL-D4`는 `.NET` feature-map에서 구현 상태이지만 Kotlin에는 gap으로 남아 있다.

수정 방향:

- `Server/Consumer` role을 추가한다.
- Client는 HTTP client와 process control만 담당한다.
- 현재 Client에 들어간 consumer 역할과 registry query 흐름을 `.NET` role 구조에 맞춰 분리한다.
- `RL-A4`, `RL-B2`, `RL-C2`, `RL-C4`, `RL-D2`, `RL-D4`의 Kotlin gap 상태를 `.NET` 구현 상태와 다시
  비교하고, public API 또는 harness 사유를 inventory에 명확히 적는다.

### 4. `RuntimeMonitoring`

현재 문제:

- Kotlin Client가 `@EnableZLinkFramework`와 `ZLinkClient` 기반이다.
- `.NET` 기준 Client driver 구조와 다르다.

수정 방향:

- Client는 HTTP endpoint와 evidence endpoint를 호출하는 driver로 바꾼다.
- 현재 server role 분리는 보존하되, Client가 framework runtime에 참여하지 않도록 정리한다.

### 5. `SpotService`

현재 문제:

- Kotlin Client가 `@EnableZLinkFramework`, `ZLinkSpotManager`, `ClientDriverSpot` 구조였다.
- `.NET`에 없는 `Server/Publisher`가 남아 있었다.
- `ActorSessionScenarioSupport.kt`처럼 scenario ID 구현이 아닌 support/context 파일이 `Client/Scenarios`에 있었다.
- Kotlin feature-map은 `.NET` 구현 scenario 일부를 gap으로 둔다.

수정 방향:

- Client spot을 제거하고 HTTP client/stream connector driver로 바꿨다.
- `Gateway`, `MultiNode`, `Play`, `Registry`, `Session` role은 유지하되 `.NET` source role과 source file 매핑을
  다시 검증한다.
- `Publisher`는 `.NET` source role에 없고 Gateway publish endpoint가 `SM-C4`를 담당하므로 제거했다.
- scenario ID 구현이 아닌 support/context 파일은 `Client/Support`로 옮겼다.
- public contract parity 또는 spec 검토 대기 항목: `SM-A5`, `SM-B2`, `SM-B4`, `SM-D2`, `SM-D12`, `SM-D14`
- E2E/harness 대기 항목: `SM-F5`, `SM-G1`, `SM-G2`, `SM-G3`, `SM-G4`

### 6. `YieldDispatch`

현재 문제:

- Kotlin Client에는 `.NET`의 `YdE2CancellationScenario.cs`에 대응하는 `E2`/cancellation scenario 구현이 없었다.
- 기존 구현도 일부 scenario가 gap/partial로 남아 있어 `.NET` 완료 범위와 다르다.

수정 방향:

- `YD-E2`는 public `ZLinkRequestCall.yield(..., CancellationToken)`으로 구현했다.
- public cancellation API가 없으면 내부 cancel helper로 우회하지 말고 `feature-map.ko.md`와
  `porting-inventory.ko.md`에 public contract gap으로 유지한다.
- `YD-B2`, `YD-B3`, `YD-C3`, `YD-D4`, `YD-E3`도 `.NET` scenario file과 evidence marker 기준으로 다시 점검한다.

### 7. `RuntimeMonitoring` extra role

현재 문제:

- Kotlin에는 `.NET` 기준 source role에 없는 `Server/FailoverService`가 별도 role로 있다.
- `.NET` source-only 기준 role은 `FilteredService`, `Registry`, `Service`, `ThrowingService`, `Trigger`다.
  `Server/Gateway`는 산출물 흔적만 있으므로 기준 role에서 제외한다.

수정 방향:

- `FailoverService`가 공통 E2E나 `.NET` feature-map에 근거가 있는지 먼저 확인한다.
- 근거가 없으면 별도 role에서 제거하고 `.NET`의 `Service` 또는 `Trigger` 흐름 안에서 검증하도록 재분류한다.
- 같은 `RoutingId` failover 검증이 별도 process를 요구한다면 `porting-inventory.ko.md`에 `.NET` 기준 파일과
  공통 scenario ID 근거를 명시한다.

### 8. `SpotService` root role switch

현재 문제:

- Kotlin `SpotService`에는 root `src/main/kotlin/.../Program.kt`가 남아 있었고,
  `ZLINK_KOTLIN_E2E_ROLE` 값으로 `registry`, `play`, `publisher`, `client`, `session`을 분기했다.
- 이 구조는 `.NET`처럼 role별 project/process entry를 두는 기준과 다르다.

수정 방향:

- root role-switch entrypoint를 제거했다.
- `Client`, `Server/Registry`, `Server/Play`, `Server/Gateway`, `Server/MultiNode`, `Server/Session`의 전용
  entrypoint만 남긴다.
- `Publisher`와 root role switch는 `.NET` 기준 근거를 확인한 뒤 제거했다.

### 9. `PubSub`

현재 문제:

- `.NET`은 PS-A4 subscriber reconnect와 PS-B2 publisher restart lifecycle 제어를 Client scenario/support
  안에서 수행한다.
- Kotlin도 Client support에 `ServerProcessLauncher`를 두고, PS-A4 reconnect subscriber와 PS-B2 restarted
  publisher를 scenario 안에서 시작하고 종료한다.
- runner는 초기 registry/publisher/subscriber 시작, client 실행, cleanup만 담당한다.

수정 방향:

- PS-A4와 PS-B2의 process lifecycle control을 Kotlin Client scenario/support 책임으로 옮겼다.
- publisher role에는 PS-B2가 HTTP 경계에서 process down 상태를 확인할 수 있도록 `/shutdown` operational
  endpoint를 추가했다.
- `timeout 420s ./run_e2e.sh` 통과 로그: `framework/languages/java/e2e-kotlin/PubSub/logs/20260702-063516-76921`

## 작업 체크리스트

- [x] `RegistrationCodec` Client를 HTTP driver로 바꾸고 helper를 `Client/Support`로 재분류한다.
- [x] `ResilienceLifecycle`에 `Server/Consumer`를 추가하고 Client의 framework 참여를 제거한다.
- [x] `RuntimeMonitoring` Client를 HTTP driver로 바꾼다.
- [x] `RuntimeMonitoring` `FailoverService` extra role의 근거를 확인하고 제거 또는 재분류한다.
- [x] `SpotService` Client spot을 제거하고 `Publisher`와 root role-switch entrypoint를 정리한다.
- [x] `SpotService` gap scenario 목록을 public contract gap과 harness gap으로 다시 분류한다.
- [x] `RegistryMessaging`에 `backpressure-consumer` role과 Client option을 추가한다.
- [x] `YieldDispatch` YD-E2와 다른 gap scenario를 public API 기준으로 다시 판정한다.
- [x] `PubSub` PS-A4/PS-B2 lifecycle orchestration을 Client support로 옮긴다.
- [x] 각 config의 `run_e2e.sh`에 공통 local E2E 대기 기준을 적용한다.
- [x] 각 config의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 실제 구현 상태와 맞춘다.
- [x] framework 기능 누락이나 버그가 나오면 원인을 고치고 회귀테스트를 추가한다.
- [x] 각 config의 `run_e2e.sh`를 실행하고 scenario marker와 role process evidence를 확인한다.

## 완료 확인 절차

1. `.NET` 기준 파일과 Kotlin 파일 매핑이 `porting-inventory.ko.md`에 갱신되어 있다.
2. `feature-map.ko.md`의 done/gap 상태가 실제 구현과 일치한다.
3. config의 실제 runner를 실행했다.
   - 기본 기준: `timeout 420s ./run_e2e.sh`
   - 긴 full sweep이 필요한 config는 기존 language plan의 timeout을 따른다.
4. 로그에서 scenario marker와 role process evidence를 확인했다.
5. read-only review로 Client driver, server role, extra role, scenario file 분류, public API gap을 다시 확인했다.

## Codex 반복 리뷰 체크

마지막에는 Codex 에이전트로 이 문서를 기준으로 반복 리뷰한다.

- [x] `.NET` source-only inventory와 Kotlin inventory를 다시 대조한다.
- [x] Client가 framework runtime으로 뜨는 항목이 남아 있는지 검색한다.
- [x] `.NET` source role이 빠졌거나 Kotlin extra role이 남았는지 확인한다.
- [x] scenario file 분류가 `.NET Client/Scenarios`와 공통 E2E scenario ID에 대응되는지 확인한다.
- [x] public API gap을 내부 helper나 test-only adapter로 숨긴 항목이 없는지 확인한다.
- [x] framework 기능 누락 또는 버그를 E2E 코드 우회로 처리한 항목이 없는지 확인한다.
- [x] 누락 항목이 나오면 이 문서의 수정 목록과 체크리스트에 추가한 뒤 다시 리뷰한다.
- [x] Codex 리뷰 결과가 `NO MISSING KOTLIN ITEMS`가 될 때까지 반복한다.

## Codex 반복 리뷰 결과

2026-07-02 source-only 재검토 결과: `NO MISSING KOTLIN ITEMS`.

이 결과는 `.NET` source role, Kotlin source role, Client runtime 참여 여부, scenario file 분류,
public API gap 처리, runner evidence를 다시 대조했을 때 새로 발견된 미분류 누락 항목이 없다는 뜻이다.
이미 문서화한 gap은 완료로 숨기지 않는다. `ResilienceLifecycle`에는 public API와 harness 대기 gap,
`SpotService`에는 public contract, spec, harness 대기 gap, `YieldDispatch`에는 partial/harness/evidence
surface gap이 남아 있으며 각 config의 `feature-map.ko.md`와 `porting-inventory.ko.md`에 사유를 적었다.

확인한 evidence:

- `RegistrationCodec`: `timeout 420s ./run_e2e.sh`, `logs/20260702-071748-33038`
- `ResilienceLifecycle`: `timeout 420s ./run_e2e.sh`, `logs/20260702-064114-7570`
- `RuntimeMonitoring`: `timeout 420s ./run_e2e.sh`, `logs/20260702-064114-7626`
- `SpotService`: `timeout 420s ./run_e2e.sh`, `logs/20260702-071019-20091`
- `RegistryMessaging`: `timeout 420s ./run_e2e.sh`, `logs/20260702-063718-91897`
- `YieldDispatch`: `timeout 420s ./run_e2e.sh`, `logs/20260702-064611-27912`
- `PubSub`: `timeout 420s ./run_e2e.sh`, `logs/20260702-063516-76921`
