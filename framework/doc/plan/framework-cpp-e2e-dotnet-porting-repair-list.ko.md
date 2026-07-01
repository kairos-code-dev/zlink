# C++ Framework E2E .NET 기준 재포팅 수정 목록

## 목적

이 문서는 C++ framework E2E를 `framework/languages/dotnet/e2e` 기준으로 다시 대조했을 때, 포팅이
아니라 다른 구조로 구현된 항목을 수정 계획으로 정리한다.

## 공통 기준

1. `.NET` 기준 source-only inventory를 먼저 만든다. `bin`, `obj`, `logs` 산출물은 기준 role에서 제외한다.
2. Client는 HTTP client 또는 stream connector driver여야 한다. C++ `app_t`, `hosted_service_t`,
   framework channel client, route mesh server로 직접 구동되면 수정 대상이다.
3. Server role은 `.NET`의 role/process 의미와 맞춘다. Extra role은 근거를 확인하고 없으면 제거하거나
   gap으로 분리한다.
4. Scenario 파일은 `.NET Client/Scenarios`와 공통 E2E scenario ID에 맞춘다.
5. public API가 없으면 내부 helper, raw frame, 테스트 전용 adapter로 우회하지 않는다.

## Framework 기능 누락과 버그 처리 원칙

누락된 C++ E2E 기능을 구현하는 중 framework 자체의 public 기능이 없거나 framework 버그가 드러나면,
E2E 코드에서 우회하지 않는다. 먼저 원인을 확인하고, 필요한 framework 기능을 같은 public contract 기준으로
추가하거나 framework 버그를 수정한다.

완료 조건은 다음을 모두 포함한다.

1. 문제 원인이 E2E harness, C++ binding, framework runtime, public API 중 어디에 있는지 확인한다.
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

### 1. `DiscoveryRegistryHa`

현재 문제:

- `.NET`에는 `DiscoveryRegistryHa` config가 있고, `Client`, `Shared`, `Server/Consumer`,
  `Server/Embedded`, `Server/Probe`, `Server/Provider`, `Server/Registry` source role이 있다.
- C++에는 `framework/languages/cpp/e2e/DiscoveryRegistryHa` config 자체가 없다.

수정 방향:

- C++ `DiscoveryRegistryHa` config를 새로 추가한다.
- `.NET` 기준 `Client/Scenarios`, `Client/Support`, `Shared`, `Server/Consumer`, `Server/Embedded`,
  `Server/Probe`, `Server/Provider`, `Server/Registry`를 source-only inventory로 먼저 매핑한다.
- 구현할 수 없는 항목은 public contract gap으로 남기고, 다른 config나 sample 이름으로 대체하지 않는다.

### 2. `RegistrationCodec`

현재 문제:

- C++ Client가 `app_t`, `add_zlink_framework`, `enable_client`로 framework application이 된다.
- `.NET` Client는 HTTP client driver다.
- C++ Server는 단일 server host에서 mode를 바꿔 invalid mode를 처리한다.
- `.NET` 기준은 `Server/Main`, `Server/InvalidDuplicate`, `Server/JsonOnlyPeer`, `Server/CodecRequester` role
  분리다.
- `.NET` runner는 `Server/CodecRequester` project를 별도 process로 시작하고 Client에는
  `--codec-requester-url`을 넘긴다. C++ runner는 별도 `CodecRequester` role 대신 같은 Client의 `B5` mode가
  JSON-only peer를 직접 호출한다.

수정 방향:

- Client를 C++ ZLink HTTP client 또는 기존 HTTP support 기반 driver로 바꾼다.
- 단일 server host의 mode 전환 구조와 Client `B5` requester 겸용 구조를 role별 executable/source tree로
  분리한다.
- `Server/CodecRequester`에 대응하는 C++ role을 추가하고, codec mismatch scenario는 `.NET`처럼 Client가
  codec requester HTTP endpoint를 호출하는 구조로 맞춘다.
- RC-A2처럼 public API gap인 항목은 현재처럼 gap으로 둘 수 있지만, gap이 아닌 role 구조 차이는 수정한다.

### 3. `ResilienceLifecycle`

현재 문제:

- C++ Client가 `hosted_service_t`, `channel_client_t`, `app_t`, `add_zlink_framework`를 쓰는 framework host다.
- `.NET` Client는 HTTP client와 process manager 기반 driver다.

수정 방향:

- Client framework host를 제거한다.
- Consumer, Provider, Registry server role은 유지하되, Client는 HTTP endpoint 호출과 process orchestration만
  담당하도록 바꾼다.
- `ResilienceProcessManager`에 해당하는 책임은 runner나 Client support로 분리하고 framework runtime에는
  참여하지 않는다.

### 4. `SpotService`

현재 문제:

- 일부 mode는 HTTP/stream scenario 함수로 실행되지만, 나머지/default path에서 Client가 `app_t`,
  `add_zlink_framework`, server/client channel, fanout publisher, route mesh, spot mesh를 직접 구성한다.
- `.NET` SpotService Client는 끝까지 HTTP client/stream connector driver다.

수정 방향:

- Client의 fallback framework participant block을 제거한다.
- 남은 mode도 모두 HTTP client 또는 stream connector scenario 함수로 연결한다.
- Client가 server channel, route mesh server, spot mesh router, publisher 역할을 직접 구성하지 않도록
  server role로 이동한다.

### 5. `YieldDispatch`

현재 문제:

- C++ Client에는 `.NET`의 `YdE2CancellationScenario.cs`에 대응하는 scenario file이 없다.
- 기존 C++ 문서상 `YD-E2`는 public cancellation token 계약 gap으로 남아 있다.

수정 방향:

- public C++ `yield` cancellation 계약이 없으면 계속 gap으로 유지한다.
- gap 상태는 `feature-map.ko.md`, `porting-inventory.ko.md`, language plan이 같은 문구로 설명해야 한다.
- 계약이 확정되기 전에는 내부 pending cancel이나 timeout cleanup으로 fake scenario를 만들지 않는다.

### 6. `RuntimeMonitoring`

현재 문제:

- `.NET` `RuntimeMonitoring` Client는 option을 읽고 scenario 함수만 실행하는 driver다.
- C++ `RuntimeMonitoring` Client는 `app_t`, `hosted_service_t`, `channel_client_t`, `add_zlink_framework`를
  사용해 framework participant로 구동된다.

수정 방향:

- C++ Client를 framework host에서 HTTP/evidence driver로 바꾼다.
- framework channel request가 필요하면 `.NET`처럼 server role의 HTTP endpoint 뒤로 숨기고, Client는
  role endpoint 호출과 scenario assertion만 담당한다.

### 7. `RegistryMessaging`

현재 문제:

- `.NET` `RegistryMessaging` Client는 `ZLinkHttpClient` 기반 driver다.
- C++ `RegistryMessaging` Client는 `hosted_service_t`, `channel_client_t`, `add_zlink_framework`를 사용해
  framework participant로 구동된다.
- `RM-C3`에서 `.NET`은 `/profile/batch-request`에 배열 payload를 보내지만, C++은 `/profile/request`를
  여러 번 호출한다.

수정 방향:

- C++ Client를 HTTP driver로 바꾼다.
- `RM-C3`는 `.NET`과 같은 batch endpoint 요청 형식을 사용하도록 맞춘다.
- C++ HTTP binding에서 array body를 public하게 처리할 수 없다면 feature-map에 public contract 또는 HTTP
  binding gap으로 남기고, 반복 single request를 1:1 완료로 표시하지 않는다.

### 8. `DeliveryDispatch`

현재 문제:

- 이 repair 문서의 기준은 `framework/languages/dotnet/e2e`지만, C++에는 `.NET` E2E 기준 config가 아닌
  `DeliveryDispatch` E2E config가 있다.
- C++ `DeliveryDispatch` 문서는 기준을 `framework/languages/dotnet/samples/DeliveryDispatch`로 둔다.
- 따라서 이 config는 framework E2E `.NET` 기준 재포팅 대상인지, sample 기반 E2E인지 분류가 필요하다.

수정 방향:

- `DeliveryDispatch`를 framework E2E `.NET` 기준 재포팅 scope에서 제외할지, 별도 sample-derived E2E로
  유지할지 문서에 명시한다.
- 유지한다면 sample porting plan 또는 별도 DeliveryDispatch plan에서 기준과 role/source layout을 관리한다.
- framework E2E 목록에 남길 경우에는 `.NET e2e` baseline이 없다는 사실과 extra role/process shape를
  feature-map과 plan에 명확히 표시한다.

### 9. `PubSub`

현재 문제:

- `.NET`은 PS-A4 subscriber reconnect와 PS-B2 publisher restart lifecycle 제어를 Client scenario/support
  안에서 수행한다.
- C++ inventory는 `.NET` `Client/Support/ServerProcessLauncher.cs`를 `run_e2e.sh`로 매핑하고 done으로
  표시한다.
- C++ runner가 subscriber/publisher stop/restart를 수행하고, C++ Client scenario는 file/env phase를 통해
  publish와 evidence 확인만 조율한다.

수정 방향:

- PS-A4와 PS-B2의 process lifecycle control을 C++ Client scenario/support 책임으로 옮긴다.
- runner는 process 시작, 기본 readiness, client 실행, cleanup만 담당한다.
- C++ harness 제약으로 Client scenario에서 제어할 수 없으면 `feature-map.ko.md`와
  `porting-inventory.ko.md`에 harness gap으로 남기고 done으로 표시하지 않는다.

## 작업 체크리스트

- [ ] `DiscoveryRegistryHa` config를 새로 추가하고 `.NET` source role을 inventory로 매핑한다.
- [ ] `RegistryMessaging` Client를 HTTP driver로 바꾸고 `RM-C3` batch endpoint를 맞춘다.
- [ ] `RegistrationCodec` Client를 HTTP driver로 바꾸고 `CodecRequester` role을 분리한다.
- [ ] `ResilienceLifecycle` Client framework host를 제거하고 HTTP/process driver로 바꾼다.
- [ ] `RuntimeMonitoring` Client framework participant 구조를 제거한다.
- [ ] `SpotService` Client fallback framework participant block을 제거한다.
- [ ] `YieldDispatch` YD-E2 gap 문구를 feature-map, inventory, language plan에 같은 의미로 맞춘다.
- [ ] `DeliveryDispatch`를 framework E2E scope에서 제외할지 sample-derived E2E로 둘지 명시한다.
- [ ] `PubSub` PS-A4/PS-B2 lifecycle orchestration을 Client support로 옮긴다.
- [ ] 각 config의 `run_e2e.sh`에 공통 local E2E 대기 기준을 적용한다.
- [ ] 각 config의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 실제 구현 상태와 맞춘다.
- [ ] framework 기능 누락이나 버그가 나오면 원인을 고치고 회귀테스트를 추가한다.
- [ ] 각 config의 `run_e2e.sh`를 실행하고 scenario marker와 role process evidence를 확인한다.

## 완료 확인 절차

1. `.NET` 기준 파일과 C++ 파일 매핑이 `porting-inventory.ko.md`에 갱신되어 있다.
2. `feature-map.ko.md`의 done/gap 상태가 실제 구현과 일치한다.
3. config의 실제 runner를 실행했다.
   - 기본 기준: `timeout 420s ./run_e2e.sh`
   - 긴 full sweep이 필요한 config는 기존 language plan의 timeout을 따른다.
4. 로그에서 scenario marker와 role process evidence를 확인했다.
5. read-only review로 Client driver, server role, extra role, scenario file 분류, public API gap을 다시 확인했다.

## Codex 반복 리뷰 체크

마지막에는 Codex 에이전트로 이 문서를 기준으로 반복 리뷰한다.

- [ ] `.NET` source-only inventory와 C++ inventory를 다시 대조한다.
- [ ] Client가 framework runtime으로 뜨는 항목이 남아 있는지 검색한다.
- [ ] `.NET` source role이 빠졌거나 C++ extra role이 남았는지 확인한다.
- [ ] scenario file 분류가 `.NET Client/Scenarios`와 공통 E2E scenario ID에 대응되는지 확인한다.
- [ ] public API gap을 내부 helper나 test-only adapter로 숨긴 항목이 없는지 확인한다.
- [ ] framework 기능 누락 또는 버그를 E2E 코드 우회로 처리한 항목이 없는지 확인한다.
- [ ] 누락 항목이 나오면 이 문서의 수정 목록과 체크리스트에 추가한 뒤 다시 리뷰한다.
- [ ] Codex 리뷰 결과가 `NO MISSING CPP ITEMS`가 될 때까지 반복한다.
