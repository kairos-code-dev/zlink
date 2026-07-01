# Node.js Framework E2E .NET 기준 재포팅 수정 목록

## 목적

이 문서는 Node.js framework E2E를 `framework/languages/dotnet/e2e` 기준으로 다시 대조했을 때, 포팅이
아니라 다른 구조로 구현된 항목을 수정 계획으로 정리한다.

## 공통 기준

1. `.NET` 기준 source-only inventory를 먼저 만든다. `bin`, `obj`, `logs` 산출물은 기준 role에서 제외한다.
2. Client는 HTTP client 또는 stream connector driver여야 한다. Framework runtime participant로 직접
   구동되면 수정 대상이다.
3. Server role은 `.NET`의 role/process 의미와 맞춘다. Extra role은 근거를 확인하고 없으면 제거하거나
   gap으로 분리한다.
4. Scenario 파일은 `.NET Client/Scenarios`와 공통 E2E scenario ID에 맞춘다.
5. public API가 없으면 내부 helper, raw frame, 테스트 전용 adapter로 우회하지 않는다.

## Framework 기능 누락과 버그 처리 원칙

누락된 Node.js E2E 기능을 구현하는 중 framework 자체의 public 기능이 없거나 framework 버그가 드러나면,
E2E 코드에서 우회하지 않는다. 먼저 원인을 확인하고, 필요한 framework 기능을 같은 public contract 기준으로
추가하거나 framework 버그를 수정한다.

완료 조건은 다음을 모두 포함한다.

1. 문제 원인이 E2E harness, Node.js binding, framework runtime, public API 중 어디에 있는지 확인한다.
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

### 1. `RegistrationCodec` extra role

처리 전 문제:

- Node에는 `.NET`에 없는 `Server/MessagePackPeer`, `Server/ProtobufPeer` role이 있다.
- `.NET` 기준 source role은 `Server/Main`, `Server/InvalidDuplicate`, `Server/JsonOnlyPeer`,
  `Server/CodecRequester`다.

처리 결과:

- `Server/MessagePackPeer`, `Server/ProtobufPeer`는 `.NET` source role에 대응하지 않으므로 Node
  `RegistrationCodec` E2E source와 runner에서 제거했다.
- `RC-B2`, `RC-B3` 단일 codec 검증은 `.NET` 기준과 같은 `Server/Main` role의 전역 codec registry에서
  실행한다.
- 별도 codec 확장 검증이 필요하면 현재 `.NET` 기준 포팅 config가 아니라 별도 draft 또는 별도 config로
  분리한다.

### 2. `DiscoveryRegistryHa`

처리 전 문제:

- HTTP driver 방향은 맞다.
- Node는 여러 DR scenario를 `Client/Scenarios/basic-discovery-scenario.ts` 하나에 모아 두었다.
- `.NET` 기준은 scenario file 단위가 더 세분되어 있다.
- Node `run_e2e.sh`의 기본 scenario는 `DR-A1`이지만, `.NET` runner 기본값은 `all`이고 모든 DR scenario를
  열거해 실행한다.

처리 결과:

- `runDrA1`, `runDrA2`, `runDrA3`, `runDrA4` 등 scenario 함수를 `.NET` scenario file과 공통 scenario ID에
  맞춰 개별 파일로 나누었다.
- 공통 helper는 `Client/Support/discovery-scenario-support.ts`로 옮겼다.
- `Client/main.ts`는 scenario 목록과 실행 순서만 가진다.
- bare `./run_e2e.sh`는 `.NET`처럼 full sweep을 실행한다. focused 실행은 별도 scenario option으로
  유지한다.

### 3. `RuntimeMonitoring`

처리 전 문제:

- `.NET` 기준 `Server/FilteredService`와 `Server/ThrowingService`는 별도 source role이다.
- Node는 두 role을 별도 folder와 entrypoint로 두지 않고 `Server/Service` main에 option을 넘겨 실행한다.

처리 결과:

- `Server/FilteredService`와 `Server/ThrowingService`를 별도 Node role folder와 entrypoint로 분리했다.
- 공통 service host 구현은 공유하지만, role entrypoint와 runner process는 `.NET` 기준과 같은 의미로
  나눈다.
- runner는 `--socket-filter`, `--throw-monitor` option으로 같은 executable을 다른 role처럼 쓰지 않는다.

### 4. `RegistrationCodec` scenario file

처리 전 문제:

- `.NET` Client는 registration/codec scenario를 개별 scenario file로 둔다.
- Node는 `rc-a-registration-scenarios.ts` 하나에 RC-A1~RC-A6을 묶고,
  `rc-b-codec-scenarios.ts` 하나에 RC-B1~RC-B5를 묶는다.

처리 결과:

- `.NET` scenario file 이름과 공통 scenario ID에 맞춰 Node scenario file을 분리했다.
- `AutoRegistration`, `AttributeRegistration`, `ManualRegistration`, `RcA4`, `RcA5`, `InvalidRegistration`,
  `RcB1`, `RcB2`, `RcB3`, `RcB4`, `CodecMismatch`에 대응하는 파일을 각각 둔다.
- 공통 HTTP/evidence helper는 `Client/Support`에 두고, scenario 파일은 scenario 하나의 검증 흐름만
  가진다.

### 5. `YieldDispatch`

처리 전 문제:

- Node language plan의 표준 구조는 support를 `Server/<Role>/Support` 아래에 둔다.
- Node `YieldDispatch`는 `Server/Support/evidence-store.ts`, `Server/Support/http-server.ts`를 top-level
  shared server support로 두고, `Delay`, `Play`, `Session` role이 `../Support/*`를 import한다.
- `.NET` 기준 support는 `Server/Delay`, `Server/Play`, `Server/Session/Support`처럼 role-local 위치에 있다.

처리 결과:

- `Server/Support`를 없애고 role-local `Server/<Role>/Support`로 나누었다.
- evidence store와 HTTP support는 role 내부 state/readiness를 다루므로 `Delay`, `Play`, `Registry`,
  `Session` role-local support로 분리했다.
- 여러 role이 공유해도 되는 순수 타입이나 wire contract는 계속 `Shared`에 둔다.

### 6. `SpotService`

확인 결과:

- Node `SpotService` Client에는 `SM-F4`, `SM-G1`, `SM-G3`, `SM-G4`, `SM-Q9` scenario가 등록되어 있었지만,
  기존 `all` 실행 목록에서는 빠져 있었다.
- `.NET` `SpotService` runner의 full sweep은 `SM-F4`, `SM-G1`, `SM-G3`, `SM-G4`, `SM-Q9`에 대응하는 child
  group을 실행한다.
- Node runner를 같은 child group 구조로 맞췄고 `all` PASS 로그를 남겼다.

수정 방향:

- Node `all` mode가 `.NET` full sweep과 같은 scenario 범위를 실행하도록 `SM-F4`, `SM-G1`, `SM-G3`,
  `SM-G4`, `SM-Q9`를 포함한다.
- focused mode는 유지하되, full sweep에서 빠진 scenario를 done으로 보지 않는다.
- runner와 `feature-map.ko.md`, `porting-inventory.ko.md`의 완료 범위를 같은 scenario 목록으로 맞춘다.

## 작업 체크리스트

- [x] `RegistrationCodec` MessagePack/Protobuf extra role의 근거를 확인하고 제거 또는 별도 config로 분리한다.
- [x] `RegistrationCodec` scenario 파일을 `.NET` scenario file 단위로 나눈다.
- [x] `DiscoveryRegistryHa` scenario 파일을 개별 파일로 나누고 bare runner 기본값을 full sweep으로 맞춘다.
- [x] `RuntimeMonitoring` `FilteredService`, `ThrowingService`를 별도 role로 분리한다.
- [x] `YieldDispatch` top-level `Server/Support`를 role-local support 또는 `Shared`로 재분류한다.
- [x] `SpotService` all mode에 `SM-F4`, `SM-G1`, `SM-G3`, `SM-G4`, `SM-Q9`를 포함한다.
- [x] 각 config의 `run_e2e.sh`에 공통 local E2E 대기 기준을 적용한다.
- [x] 각 config의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 실제 구현 상태와 맞춘다.
- [x] framework 기능 누락이나 버그가 나오면 원인을 고치고 회귀테스트를 추가한다.
- [x] 각 config의 `run_e2e.sh`를 실행하고 scenario marker와 role process evidence를 확인한다.

## 완료 확인 절차

1. `.NET` 기준 파일과 Node.js 파일 매핑이 `porting-inventory.ko.md`에 갱신되어 있다.
2. `feature-map.ko.md`의 done/gap 상태가 실제 구현과 일치한다.
3. config의 실제 runner를 실행했다.
   - 기본 기준: `timeout 420s ./run_e2e.sh`
   - 긴 full sweep이 필요한 config는 기존 language plan의 timeout을 따른다.
4. 로그에서 scenario marker와 role process evidence를 확인했다.
5. read-only review로 Client driver, server role, extra role, scenario file 분류, public API gap을 다시 확인했다.

## Codex 반복 리뷰 체크

마지막에는 Codex 에이전트로 이 문서를 기준으로 반복 리뷰한다.

- [x] `.NET` source-only inventory와 Node.js inventory를 다시 대조한다.
- [x] Client가 framework runtime으로 뜨는 항목이 남아 있는지 검색한다.
- [x] `.NET` source role이 빠졌거나 Node.js extra role이 남았는지 확인한다.
- [x] scenario file 분류가 `.NET Client/Scenarios`와 공통 E2E scenario ID에 대응되는지 확인한다.
- [x] public API gap을 내부 helper나 test-only adapter로 숨긴 항목이 없는지 확인한다.
- [x] framework 기능 누락 또는 버그를 E2E 코드 우회로 처리한 항목이 없는지 확인한다.
- [x] 누락 항목이 나오면 이 문서의 수정 목록과 체크리스트에 추가한 뒤 다시 리뷰한다.
- [x] Codex 리뷰 결과가 `NO MISSING NODE ITEMS`가 될 때까지 반복한다.

리뷰 결과: `NO MISSING NODE ITEMS`
