# Java Framework E2E .NET 기준 재포팅 수정 목록

## 목적

이 문서는 Java framework E2E를 `framework/languages/dotnet/e2e` 기준으로 다시 대조했을 때, 포팅이
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

누락된 Java E2E 기능을 구현하는 중 framework 자체의 public 기능이 없거나 framework 버그가 드러나면,
E2E 코드에서 우회하지 않는다. 먼저 원인을 확인하고, 필요한 framework 기능을 같은 public contract 기준으로
추가하거나 framework 버그를 수정한다.

완료 조건은 다음을 모두 포함한다.

1. 문제 원인이 E2E harness, Java binding, framework runtime, public API 중 어디에 있는지 확인한다.
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

- `.NET`은 dynamic provider lifecycle 제어를 Client support/scenario 안에서 수행한다.
- Java는 `RM-B1`, `RM-B2`, `RM-A4` provider start/stop orchestration을 `run_e2e.sh`가 수행하고, Client
  scenario는 file signal로 runner와 조율한다.
- `porting-inventory.ko.md`는 이 runner delegation을 done으로 기록해 scenario/support 책임 차이가 드러나지
  않는다.

수정 방향:

- `RM-B1`, `RM-B2`, `RM-A4` provider lifecycle control을 Java Client scenario/support 책임으로 옮긴다.
- runner는 기본 process 시작, readiness, client 실행, cleanup만 담당한다.
- Java harness 제약으로 Client scenario에서 제어할 수 없으면 `feature-map.ko.md`와
  `porting-inventory.ko.md`에 harness gap으로 남기고 done으로 표시하지 않는다.

### 2. `RegistrationCodec`

현재 문제:

- `.NET` Client는 `ZLinkHttpClient`로 scenario를 실행한다.
- Java Client는 `@EnableZLinkFramework`, `ZLinkFrameworkConfigurer`, `ZLinkClient` 주입으로 framework
  participant가 된다.

수정 방향:

- Client를 framework application에서 HTTP client driver로 바꾼다.
- codec registration, invalid duplicate, JSON-only peer, codec requester 책임은 `Server/<Role>`로 옮긴다.
- `.NET`의 `Server/Main`, `Server/InvalidDuplicate`, `Server/JsonOnlyPeer`, `Server/CodecRequester` source를
  Java role에 각각 매핑한다.

### 3. `ResilienceLifecycle`

현재 문제:

- `.NET` Client는 여러 HTTP client와 process manager로 consumer, registry, provider role을 조작한다.
- Java Client는 framework app으로 뜨고 `ZLinkClient`, `ZLinkRegistryQueryClient`를 직접 사용한다.
- `.NET` 기준 `Server/Consumer` source role이 Java에는 없다.

수정 방향:

- `Server/Consumer` role을 추가하고 Client에 섞인 consumer/framework request 책임을 server role로 옮긴다.
- Client는 HTTP client와 process control support만 가진다.
- `.NET` scenario file 목록에 맞춰 monolithic `ClientScenario`를 개별 scenario 파일로 나눈다.

### 4. `RuntimeMonitoring`

현재 문제:

- `.NET` Client는 monitoring scenario를 driver로 실행한다.
- Java Client는 `@EnableZLinkFramework`와 `ZLinkClient` 기반 직접 request 구조다.
- `.NET` 기준 `Server/FilteredService`, `Server/ThrowingService` role이 Java에는 없다.
- Java Client 출력 범위는 `.NET`의 `MON-A4`, `MON-B2`, `MON-D1` 흐름까지 1:1로 대응하지 않는다.

수정 방향:

- Client를 framework participant에서 HTTP driver로 바꾼다.
- `Server/FilteredService`, `Server/ThrowingService` role을 source role로 추가한다.
- `MON-A1`부터 `MON-D1`까지 `.NET` scenario file과 공통 E2E scenario ID를 다시 매핑한다.

### 5. `SpotService`

현재 문제:

- `.NET` Client는 HTTP client와 stream connector 기반 driver다.
- Java Client는 `@EnableZLinkFramework`, `ZLinkSpotManager`, `ClientDriverSpot`로 spot을 직접 띄운다.
- Java server role에는 `.NET` 기준 `Gateway`, `MultiNode`, `Session`이 없고, `.NET`에 없는 `Publisher`가 있다.

수정 방향:

- Client spot을 제거하고 HTTP client/stream connector driver로 재작성한다.
- `Gateway`, `MultiNode`, `Play`, `Registry`, `Session` role을 `.NET` 기준으로 나눈다.
- `Publisher` role은 공통 E2E나 `.NET` 기준 근거가 없으면 제거하거나 `not-needed`로 명시한다.

### 6. `YieldDispatch`

현재 문제:

- stream connector 사용 자체는 맞다.
- `.NET`의 `Client/Scenarios/*.cs` 기준과 달리 대부분 scenario가 단일 `Program.java`에 모여 있다.
- `YD-E2` cancellation cleanup도 별도 scenario file이 아니라 `runCancellationCleanup()`에 있다.

수정 방향:

- 기능 구현은 최대한 보존하되, `.NET` Client scenario file 단위로 Java `Client/Scenarios`를 나눈다.
- `Program.java`는 scenario 목록과 실행 순서만 선언하도록 줄인다.
- `Client/Support`에는 stream connector 생성, evidence wait, assertion helper만 남긴다.

### 7. `DiscoveryRegistryHa`

현재 문제:

- Java feature map은 `DR-B2`, `DR-B3`, `DR-C1`, `DR-C2`를 runtime gap으로 표시하지만, `.NET` 기준 feature
  map은 같은 scenario를 구현 상태로 둔다.
- `Client/Scenarios/ClientScenario.java`는 scenario ID 하나를 실행하는 파일이 아니라 dispatch용 interface다.
  현재 위치는 scenario ID 구현 파일만 둔다는 분류 규칙과 맞지 않는다.

수정 방향:

- `DR-B2`, `DR-B3`, `DR-C1`, `DR-C2`를 `.NET` scenario와 공통 E2E 문서 기준으로 다시 구현하거나, Java
  public API 또는 harness 한계가 있으면 feature-map에 정확한 public contract gap으로 남긴다.
- `ClientScenario.java`는 `Client/Support`로 옮기거나 support 파일로 재분류한다.
- `porting-inventory.ko.md`에는 `.NET` scenario file과 Java scenario/support 파일을 구분해 기록한다.

### 8. `PubSub`

현재 문제:

- `.NET`은 PS-A4 subscriber reconnect와 PS-B2 publisher restart lifecycle 제어를 Client scenario/support
  안에서 수행한다.
- Java는 PS-A4/PS-B2의 stop/restart orchestration을 `run_e2e.sh`가 먼저 수행하고, Client scenario는
  runner가 만들어 둔 gap 또는 restart 이후 상태만 확인한다.
- Java inventory는 이 runner delegation을 done으로 기록하고 있다.

수정 방향:

- PS-A4와 PS-B2의 process lifecycle control을 Java Client scenario/support 책임으로 옮긴다.
- runner는 process 시작, 기본 readiness, client 실행, cleanup만 담당한다.
- Java public/process harness 제약으로 Client scenario에서 제어할 수 없으면 `feature-map.ko.md`와
  `porting-inventory.ko.md`에 harness gap으로 남기고 done으로 표시하지 않는다.

## 작업 체크리스트

- [x] `RegistrationCodec` Client를 HTTP driver로 바꾸고 server role 매핑을 갱신한다.
- [x] `ResilienceLifecycle`에 `Server/Consumer`를 추가하고 Client의 framework 참여를 제거한다.
- [x] `RuntimeMonitoring` Client를 HTTP driver로 바꾸고 `FilteredService`, `ThrowingService` role을 추가한다.
- [ ] `SpotService` Client spot을 제거하고 `Gateway`, `MultiNode`, `Session` role을 추가한다.
- [ ] `RegistryMessaging` RM-B1/RM-B2/RM-A4 lifecycle orchestration을 Client support로 옮긴다.
- [ ] `YieldDispatch` scenario를 `.NET` file 단위로 나누고 `Program.java`는 실행 목록만 남긴다.
- [ ] `DiscoveryRegistryHa` DR-B2/DR-B3/DR-C1/DR-C2 gap을 다시 판정한다.
- [ ] `PubSub` PS-A4/PS-B2 lifecycle orchestration을 Client support로 옮긴다.
- [ ] 각 config의 `run_e2e.sh`에 공통 local E2E 대기 기준을 적용한다.
- [ ] 각 config의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 실제 구현 상태와 맞춘다.
- [ ] framework 기능 누락이나 버그가 나오면 원인을 고치고 회귀테스트를 추가한다.
- [ ] 각 config의 `run_e2e.sh`를 실행하고 scenario marker와 role process evidence를 확인한다.

## 완료 확인 절차

1. `.NET` 기준 파일과 Java 파일 매핑이 `porting-inventory.ko.md`에 갱신되어 있다.
2. `feature-map.ko.md`의 done/gap 상태가 실제 구현과 일치한다.
3. config의 실제 runner를 실행했다.
   - 기본 기준: `timeout 420s ./run_e2e.sh`
   - 긴 full sweep이 필요한 config는 기존 language plan의 timeout을 따른다.
4. 로그에서 scenario marker와 role process evidence를 확인했다.
5. read-only review로 Client driver, server role, extra role, scenario file 분류, public API gap을 다시 확인했다.

## Codex 반복 리뷰 체크

마지막에는 Codex 에이전트로 이 문서를 기준으로 반복 리뷰한다.

- [ ] `.NET` source-only inventory와 Java inventory를 다시 대조한다.
- [ ] Client가 framework runtime으로 뜨는 항목이 남아 있는지 검색한다.
- [ ] `.NET` source role이 빠졌거나 Java extra role이 남았는지 확인한다.
- [ ] scenario file 분류가 `.NET Client/Scenarios`와 공통 E2E scenario ID에 대응되는지 확인한다.
- [ ] public API gap을 내부 helper나 test-only adapter로 숨긴 항목이 없는지 확인한다.
- [ ] framework 기능 누락 또는 버그를 E2E 코드 우회로 처리한 항목이 없는지 확인한다.
- [ ] 누락 항목이 나오면 이 문서의 수정 목록과 체크리스트에 추가한 뒤 다시 리뷰한다.
- [ ] Codex 리뷰 결과가 `NO MISSING JAVA ITEMS`가 될 때까지 반복한다.
