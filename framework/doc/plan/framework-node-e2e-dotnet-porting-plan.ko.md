# Node Framework E2E .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/e2e`를 기준 구현으로 삼아
`framework/languages/node/e2e`를 새로 구성하는 절차를 정의한다. 목표는 단순히 Node에서 같은
scenario marker를 통과시키는 것이 아니라, `.NET` e2e와 같은 폴더 형태, 역할 분리, 파일 분류,
실행 방식, 검증 깊이를 가진 Node e2e를 만드는 것이다.

Node e2e는 기존 공용 runner 구조를 기준으로 복구하지 않는다. 이전 Node e2e는 여러 config의 테스트
본문이 한 runner에 모여 있어 `.NET` 기준의 `Client/`, `Server/`, `Shared/`, `Client/Scenarios/`,
`Client/Support/`, `Server/<Role>/...` 구조와 맞지 않았다. 이번 포팅은 `.NET` e2e의 실제 파일 트리와
공통 e2e 문서를 기준으로 처음부터 다시 배치한다.

## 완료 기준

포팅 완료는 아래 조건을 모두 만족해야 한다.

1. `framework/languages/node/e2e/<Config>/`가 대응하는 `.NET` config와 같은 의미의 폴더 구조를 가진다.
2. `.NET`에 있는 config, role, scenario, shared message, support 책임이 Node에 빠짐없이 대응된다.
3. 파일 이름은 Node 관례에 맞게 바꿀 수 있지만, 파일 분류와 책임 경계는 `.NET`과 같은 의미를 유지한다.
4. 각 scenario는 공통 e2e 문서의 scenario ID와 `.NET` client scenario 파일을 함께 대조해 구현한다.
5. Node 구현이 특정 public API 부족 때문에 같은 동작을 제공할 수 없으면 내부 helper나 raw-frame 우회로
   메우지 않는다. `feature-map.ko.md`에 public contract gap으로 기록하고 별도 설계 작업으로 분리한다.
6. 포팅 중 버그가 발생하면 scenario만 통과시키는 우회 코드를 넣지 않는다. 실패 원인을 public runtime,
   framework, stream connector, zlink http client, e2e harness 중 책임 계층까지 추적하고, 같은 문제가
   다시 생기지 않도록 회귀 테스트를 먼저 추가하거나 함께 추가한 뒤 수정한다.
7. 한 config의 build, 실행, evidence 확인, feature-map 갱신, Codex 에이전트 리뷰가 모두 끝나기 전에는
   다음 config 작업을 시작하지 않는다.
8. Codex 에이전트 리뷰에서 이슈가 나오면 같은 config 안에서 모두 수정하고 다시 리뷰를 요청한다.
   리뷰 결과가 이슈 없음일 때만 다음 config로 넘어간다.

## 기준 문서와 기준 코드

포팅할 때는 아래 순서로 기준을 확인한다.

1. 공통 e2e 문서:
   - `framework/doc/framework/common/e2e/README.ko.md`
   - `framework/doc/framework/common/e2e/config-*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/e2e/<Config>/`
   - `framework/languages/dotnet/e2e/<Config>/feature-map.ko.md`
3. Node public contract 문서:
   - `framework/doc/framework/node/spec/`
   - `framework/doc/framework/node/guide/`
   - `framework/doc/framework/node/internals/dotnet-to-node-surface-mapping.ko.md`
   - `framework/doc/framework/node/internals/node-binding-public-api-gap-list.ko.md`
4. Node runtime과 contract tests:
   - `framework/languages/node/packages/`
   - `framework/languages/node/test/contract/`

공통 e2e 문서는 검증 요구와 scenario 누락을 찾는 기준이다. 새 public API를 추가하는 근거는 아니다.
새 public API가 필요하면 먼저 spec/guide/draft 검토 대상으로 분리한다.

`.NET` e2e는 포팅의 기준 구현이지만, 모든 config가 공통 e2e 완료 기준을 이미 완전히 만족한다는 뜻은
아니다. 포팅 전에 `.NET`의 `feature-map.ko.md`를 읽고 완료, 부분 구현, public contract gap, harness
gap을 구분한다. `.NET`에서 부분 구현인 항목을 Node에서 그대로 완료로 표시하지 않는다. 공통 e2e 문서가
`.NET` 구현보다 더 강한 완료 기준을 요구하면 공통 e2e 문서를 우선하고, Node에서 바로 구현할 수 없으면
`feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남긴다.

## 표준 Node E2E 구조

각 config는 아래 구조를 따른다. 확장자는 TypeScript/JavaScript 관례에 맞추되, 역할 분리는 `.NET`과
같게 유지한다.

```text
framework/languages/node/e2e/<Config>/
|-- Shared/
|   `-- messages.ts
|-- Server/
|   |-- <Role>/
|   |   |-- package.json
|   |   |-- tsconfig.json
|   |   |-- main.ts
|   |   |-- <role>-host-factory.ts
|   |   |-- Configuration/
|   |   |-- Endpoints/
|   |   |-- Handlers/
|   |   |-- Infrastructure/
|   |   `-- Support/
|   `-- <OtherRole>/
|-- Client/
|   |-- package.json
|   |-- tsconfig.json
|   |-- main.ts
|   |-- Scenarios/
|   `-- Support/
|-- logs/
|   `-- .gitignore
|-- .gitignore
|-- feature-map.ko.md
|-- run_e2e.sh
`-- README.ko.md
```

`README.ko.md`는 `.NET` config에 있거나 Node에서 보충 설명이 필요한 경우에만 둔다. `logs/`에는
`.gitignore`만 추적한다. 빌드 산출물, `node_modules`, 임시 로그, 생성 파일은 커밋하지 않는다.

## 파일 분류 규칙

| 위치 | 책임 |
|------|------|
| `Shared/` | server와 client가 함께 쓰는 request, reply, event, evidence 타입 |
| `Client/main.ts` | scenario 목록과 실행 순서 선언 |
| `Client/Scenarios/` | scenario ID 하나마다 파일 하나 |
| `Client/Support/` | option parsing, assertion, process launcher, wait helper |
| `Server/<Role>/main.ts` | 해당 role 실행 진입점 |
| `Server/<Role>/*host-factory.ts` | NestJS app, ZLink framework, registry 설정 |
| `Server/<Role>/Configuration/` | role 실행 옵션과 환경 변수 해석 |
| `Server/<Role>/Endpoints/` | HTTP endpoint와 evidence/wait/shutdown endpoint |
| `Server/<Role>/Handlers/` | ZLink handler, observer, spot, actor handler |
| `Server/<Role>/Infrastructure/` | evidence store, in-memory state, role 내부 저장소 |
| `Server/<Role>/Support/` | 해당 role 내부에서만 쓰는 relay, wait, runtime helper |
| `run_e2e.sh` | build, 포트 할당, 프로세스 시작과 종료, 로그 수집, client 실행 |
| `feature-map.ko.md` | scenario별 구현 상태, gap, 검증 결과 |

같은 성격의 파일을 여러 위치에 섞지 않는다. 예를 들어 endpoint 일부를 `main.ts`에 두고 일부를
`Endpoints/`에 두지 않는다. `main.ts`는 실행 진입점만 남기고, host 구성은 host factory에 둔다.

## .NET 위치 복사 금지와 재분류 규칙

`.NET` e2e의 현재 파일 위치가 항상 목표 위치는 아니다. 공통 e2e README는 일부 오래된 `.NET` config가
role 루트에 option, endpoint, handler, evidence 파일을 남겨 두고 있음을 전제로, 다른 언어로 옮길 때는
현재 위치를 그대로 복사하지 말고 성격별 폴더로 재분류하라고 요구한다.

Node 포팅에서는 `.NET` 파일의 책임을 먼저 판정한 뒤 아래 기준으로 배치한다.

- option, argument, endpoint 주소 설정: `Server/<Role>/Configuration/`
- HTTP endpoint mapping, evidence wait, shutdown endpoint: `Server/<Role>/Endpoints/`
- framework handler, dispatch filter, observer, spot, actor handler: `Server/<Role>/Handlers/`
- evidence store, runtime state, in-memory repository: `Server/<Role>/Infrastructure/`
- 해당 role 내부에서만 쓰는 relay, wait, runtime helper: `Server/<Role>/Support/`
- 여러 scenario가 함께 쓰는 client-side context나 helper: `Client/Support/`
- scenario ID 하나를 실행하는 파일: `Client/Scenarios/`

따라서 `.NET`에서 role root에 있는 `EvidenceStore.cs`, `ProviderEndpoints.cs`, `ServerOptions.cs`,
`DispatchFilters.cs` 같은 파일은 Node에서 role root에 그대로 두지 않는다. `porting-inventory.ko.md`의
비고에는 원본 위치와 재분류한 목표 위치를 함께 적는다.

## Scenario ID 판정 규칙

`Client/Scenarios/` 아래에 있다는 이유만으로 모두 scenario 파일로 보지 않는다. scenario 파일은 공통
e2e 문서의 scenario ID 하나를 직접 실행하고 marker를 검증하는 파일이다. 반대로 context, shared record,
fixture, helper는 `.NET`에서 `Client/Scenarios/` 아래에 있더라도 Node에서는 `Client/Support/`나
`Shared/`로 옮긴다.

완료 판정은 파일 수가 아니라 scenario ID 기준으로 한다.

- 공통 e2e 문서의 scenario ID 목록을 먼저 만든다.
- `.NET Client/Scenarios` 파일이 어떤 scenario ID를 담당하는지 매핑한다.
- scenario ID가 없는 helper 파일은 `porting-inventory.ko.md`에서 `support` 또는 `shared`로 분류한다.
- `.NET`에 별도 scenario 파일이 없지만 공통 e2e와 `.NET feature-map`에 scenario ID가 있으면
  `porting-inventory.ko.md`에 공통 scenario ID 행을 추가하고 Node 대응 scenario 파일을 명시한다.
- `feature-map.ko.md`에는 scenario ID별 상태만 완료/부분/gap으로 기록한다.

## Inventory 매핑 산출물

각 config는 포팅 시작 시점에 `.NET` 기준 파일을 모두 펼친 뒤, Node에서 어디로 옮겼는지 기록하는
매핑 문서를 반드시 만든다.

```text
framework/languages/node/e2e/<Config>/porting-inventory.ko.md
```

이 문서는 config 완료 판정의 일부이며 커밋 대상이다. 형식은 아래 표를 사용한다.

| .NET 기준 파일 | Node 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `Client/Scenarios/...Scenario.cs` | `Client/Scenarios/...scenario.ts` | scenario | done/gap | scenario ID와 marker |
| `Server/<Role>/...` | `Server/<Role>/...` | server-role | done/gap | role 이름과 endpoint/handler 책임 |
| `Shared/Messages.cs` | `Shared/messages.ts` | shared | done/gap | payload field 대응 |
| `Client/Support/...` | `Client/Support/...` | support | done/gap | 공통 helper 책임 |

규칙:

- `.NET` 기준 파일 목록은 아래 명령으로 생성한다.

```bash
find framework/languages/dotnet/e2e/<Config> -type f \
  ! -path '*/bin/*' \
  ! -path '*/obj/*' \
  ! -path '*/logs/*' \
  | sed 's#^framework/languages/dotnet/e2e/<Config>/##' \
  | sort
```

- `.csproj`, `.gitignore`, `run_e2e.sh`, `feature-map.ko.md`, `README.ko.md`도 매핑에서 빠뜨리지 않는다.
- `.NET`의 파일 하나가 Node에서 여러 파일로 나뉘면 대응 파일 칸에 모두 적고, 왜 나뉘었는지 비고에
  적는다.
- Node에서 같은 책임이 필요 없다고 판단한 파일도 행을 삭제하지 않는다. 상태를 `gap` 또는 `not-needed`
  로 두고 public contract 근거를 비고에 적는다.
- `porting-inventory.ko.md`에 `pending` 상태가 하나라도 남아 있으면 config 완료로 보지 않는다.
- Codex 에이전트 리뷰는 이 inventory와 실제 파일 트리를 함께 대조해야 한다.

## 공통 작업 절차

각 config는 아래 절차를 처음부터 끝까지 완료해야 한다.

1. `.NET` inventory 생성
   - `find framework/languages/dotnet/e2e/<Config> -type f ! -path '*/bin/*' ! -path '*/obj/*' ! -path '*/logs/*'`
   - role, scenario, shared message, support 파일 목록을 기록한다.
   - 결과를 `porting-inventory.ko.md`의 `.NET 기준 파일` 열에 빠짐없이 옮긴다.
2. 공통 scenario 문서 대조
   - 대응하는 `config-*.ko.md`에서 scenario ID, 성공 marker, 실패 조건을 추출한다.
   - `.NET` client scenario 파일과 marker 이름이 맞는지 확인한다.
   - `.NET feature-map.ko.md`의 완료/부분/gap 상태를 함께 기록한다.
3. Node public surface 확인
   - Node spec/guide와 contract tests에서 필요한 public API가 있는지 확인한다.
   - public API가 없으면 구현하지 않고 gap으로 남길지, 별도 spec 작업이 필요한지 분리한다.
4. Node 파일 트리 생성
   - `.NET`과 같은 config/role/scenario 단위로 폴더를 만든다.
   - 빈 placeholder만 커밋하지 않는다. 각 파일은 해당 책임을 실제로 가진다.
5. Shared 포팅
   - `.NET Shared/Messages.cs`의 타입을 `Shared/messages.ts`로 옮긴다.
   - wire payload 이름, field 이름, optional 의미를 scenario 문서와 맞춘다.
6. Server role 포팅
   - `.NET Server/<Role>` 하나에 대응하는 Node role 하나를 만든다.
   - registry, provider, consumer, publisher, subscriber, play, session, delay 같은 역할은 합치지 않는다.
   - framework 기능 호출은 실제 role server endpoint 안에 둔다.
   - `.NET` role root에 있는 파일도 책임에 따라 `Configuration/`, `Endpoints/`, `Handlers/`,
     `Infrastructure/`, `Support/`로 재분류한다.
7. Client scenario 포팅
   - 공통 e2e scenario ID를 담당하는 `.NET Client/Scenarios/<Scenario>.cs` 하나에 대응하는 Node scenario
     파일 하나를 만든다.
   - scenario ID가 없는 helper/context 파일은 `Client/Scenarios/`가 아니라 `Client/Support/` 또는
     `Shared/`로 옮긴다.
   - scenario가 전체 실행을 server에 위임하지 않게 한다.
   - client는 HTTP trigger, stream connector, evidence 확인을 직접 조합해 검증한다.
8. run script 작성
   - build, 로그 디렉토리, 포트 할당, 서버 readiness, client 실행, 실패 시 로그 출력, cleanup을 포함한다.
   - timeout은 무한 대기가 없도록 bounded 값으로 둔다.
9. feature-map 갱신
   - 구현 완료, public contract gap, harness gap, 검증 명령과 결과를 scenario ID별로 기록한다.
10. inventory 갱신
    - 모든 `.NET` 기준 파일 행의 Node 대응 파일, 분류, 상태를 채운다.
    - `pending` 상태가 남아 있으면 다음 단계로 넘어가지 않는다.
11. 자체 검증
    - 해당 config의 `run_e2e.sh`를 실제 실행한다.
    - 실패하면 같은 config 안에서 수정하고 다시 실행한다.
    - 실패 원인을 모른 채 sleep, retry 횟수 증가, runner-only adapter, raw frame 조작으로 덮지 않는다.
      원인을 좁혀 framework, stream connector, zlink http client, e2e 중 책임 위치를 수정하고 회귀
      테스트를 추가한다.
12. Codex 에이전트 리뷰
    - 아래 리뷰 요청 템플릿으로 독립 리뷰를 요청한다.
    - 리뷰 결과가 이슈 없음이 될 때까지 수정과 재검증을 반복한다.
13. config 완료 선언
    - 완료한 config의 파일 목록, 실행 결과, review 결과를 작업 로그에 남긴다.
    - 그 다음 config로 넘어간다.

## Codex 에이전트 리뷰 게이트

각 config가 자체 검증을 통과하면 다음 요청으로 Codex 에이전트 리뷰를 받는다.

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/node/e2e/<Config>
기준: framework/languages/dotnet/e2e/<Config>, framework/doc/framework/common/e2e/config-*.ko.md

확인할 것:
1. .NET 기준의 폴더 구조, role 분리, 파일 분류가 Node에도 같은 의미로 반영되었는가.
2. .NET의 Client/Scenarios 파일과 공통 e2e scenario ID가 Node에서 빠짐없이 대응되는가.
3. Shared message, server role, endpoint, handler, infrastructure, client support가 누락 없이 포팅되었는가.
4. porting-inventory.ko.md가 .NET 기준 파일을 빠짐없이 담고, 각 행의 Node 대응 파일과 상태가 실제와 맞는가.
5. .NET feature-map의 부분 구현/gap 상태를 Node feature-map에서 과장 없이 반영했는가.
6. .NET role root에 있던 option/endpoint/handler/evidence/support 파일을 목표 분류로 재배치했는가.
7. Client/Scenarios 아래 helper/context 파일을 scenario로 오분류하지 않았는가.
8. public contract에 없는 기능을 private API, raw frame, test-only adapter, runner server로 우회하지 않았는가.
9. run_e2e.sh가 실제 프로세스 경계, readiness, cleanup, 실패 로그 출력을 제대로 처리하는가.
10. feature-map.ko.md가 구현 완료와 gap을 과장 없이 기록하는가.
11. 버그 수정이 임시 우회가 아니라 원인 계층을 고친 변경이며, 재발을 막는 회귀 테스트가 함께 있는가.
12. 실제 실행 결과가 문서와 코드의 완료 주장과 일치하는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```

리뷰에서 하나라도 substantive issue가 나오면 다음 config로 넘어가지 않는다. 수정 후 해당 config의
`run_e2e.sh`를 다시 실행하고, 같은 리뷰 요청을 다시 보낸다.

## 진행 순서

진행 순서는 작은 메시징 기반 config에서 시작해, registry HA와 lifecycle, monitoring, spot, yield로
확장한다. 각 단계는 이전 단계가 완전히 끝난 뒤에만 시작한다.

| 순서 | Config | 기준 문서 | 완료 조건 |
|------|--------|-----------|-----------|
| 1 | `RegistryMessaging` | `config-1-registry-messaging.ko.md` | registry, provider, workflow, consumer role과 RM-* scenario 전부 통과 |
| 2 | `PubSub` | `config-3-pubsub.ko.md` | publisher, subscriber, registry role과 fanout/reconnect/negative scenario 전부 통과 |
| 3 | `RegistrationCodec` | `config-4-registration-codec.ko.md` | manual, attribute, auto registration, codec variant, invalid registration scenario 전부 통과 |
| 4 | `DiscoveryRegistryHa` | `config-6-discovery-registry-ha.ko.md` | registry cluster, provider, consumer, embedded/direct endpoint scenario 전부 통과 |
| 5 | `ResilienceLifecycle` | `config-5-resilience-lifecycle.ko.md` | restart, remap, drain, crash, outage, observer failure scenario 전부 통과 |
| 6 | `RuntimeMonitoring` | `config-7-monitoring.ko.md` | socket, registry, spot, availability, filter, dispatch failure, recovery scenario 전부 통과 |
| 7 | `SpotService` | `config-2-spot-service.ko.md` | spot, actor, session, route, timer, multi-node scenario 전부 통과 |
| 8 | `YieldDispatch` | `config-8-yield-dispatch.ko.md` | YD-A/B/C/D/E 전체 scenario 통과. 특히 YD-D1 local topology, YD-E3 runtime shutdown, YD-E4 금지 표면 정적 검증, YD-E5 언어별 의미 동등성까지 확인 |

Node의 기존 이름이 `Monitoring`이었다면 이번 포팅에서는 `.NET`과 맞춰 `RuntimeMonitoring`을 사용한다.
`YieldDispatch`는 Node 기존 e2e에 없었더라도 `.NET`과 공통 e2e에 있으므로 별도 config로 포팅한다.

## Config별 기준 파일 목록

아래 목록은 포팅 시작 전 반드시 `.NET` checkout에서 다시 생성해 확인한다. 이 문서의 목록은 계획
작성 시점의 기준이며, 실제 작업 중 `.NET` 파일이 바뀌면 최신 `.NET` 파일 트리를 우선한다.

### RegistryMessaging

필수 분류:

- `Shared/Messages.cs`
- `Client/Program.cs`
- `Client/Scenarios/RmA1DiscoveryRequestScenario.cs`
- `Client/Scenarios/RmA2ManualEndpointScenario.cs`
- `Client/Scenarios/RmA4SameRidFailoverScenario.cs`
- `Client/Scenarios/RmA6MultipleChannelsScenario.cs`
- `Client/Scenarios/RmB1ScaleOutScenario.cs`
- `Client/Scenarios/RmB2ScaleInScenario.cs`
- `Client/Scenarios/RmC1RequestSendScenario.cs`
- `Client/Scenarios/RmC2TargetedRouteScenario.cs`
- `Client/Scenarios/RmC3MultiProviderDistributionScenario.cs`
- `Client/Scenarios/RmC4TimeoutIsolationScenario.cs`
- `Client/Scenarios/RmC5MissingPacketScenario.cs`
- `Client/Scenarios/RmC7WeightedProviderScenario.cs`
- `Client/Scenarios/RmC8PayloadRoundTripScenario.cs`
- `Client/Scenarios/RmC9BackpressureScenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/DynamicClusterLauncher.cs`
- `Client/Support/ScenarioAssert.cs`
- `Server/Registry/`
- `Server/Provider/`
- `Server/Workflow/`
- `Server/Consumer/`

### PubSub

필수 분류:

- `Shared/Messages.cs`
- `Client/Scenarios/FanoutBasicDeliveryScenario.cs`
- `Client/Scenarios/LateSubscriberScenario.cs`
- `Client/Scenarios/MissingMessageNameScenario.cs`
- `Client/Scenarios/PublisherRestartScenario.cs`
- `Client/Scenarios/SlowSubscriberScenario.cs`
- `Client/Scenarios/SubscriberReconnectScenario.cs`
- `Client/Scenarios/TopicFilterScenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/Evidence.cs`
- `Client/Support/ScenarioAssert.cs`
- `Client/Support/ServerProcessLauncher.cs`
- `Server/Registry/`
- `Server/Publisher/`
- `Server/Subscriber/`

### RegistrationCodec

필수 분류:

- `Shared/Messages.cs`
- `Client/Scenarios/ManualRegistrationScenario.cs`
- `Client/Scenarios/AttributeRegistrationScenario.cs`
- `Client/Scenarios/AutoRegistrationScenario.cs`
- `Client/Scenarios/InvalidRegistrationScenario.cs`
- `Client/Scenarios/CodecMismatchScenario.cs`
- `Client/Scenarios/RcA4DiLifecycleScenario.cs`
- `Client/Scenarios/RcA5FilterOrderingScenario.cs`
- `Client/Scenarios/RcB1JsonCodecScenario.cs`
- `Client/Scenarios/RcB2ProtobufCodecScenario.cs`
- `Client/Scenarios/RcB3MessagePackCodecScenario.cs`
- `Client/Scenarios/RcB4CodecCoexistenceScenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/CodecScenarioResult.cs`
- `Client/Support/EvidenceText.cs`
- `Client/Support/ProcessSupport.cs`
- `Client/Support/ScenarioAssert.cs`
- `Server/Main/`
- `Server/InvalidDuplicate/`
- `Server/JsonOnlyPeer/`
- `Server/CodecRequester/`

### DiscoveryRegistryHa

필수 분류:

- `Shared/Messages.cs`
- `Client/Scenarios/BasicDiscoveryScenario.cs`
- `Client/Scenarios/DrA2ClusterBridgeScenario.cs`
- `Client/Scenarios/DrA3ClusterBridgeScenario.cs`
- `Client/Scenarios/DrA4ThirdRegistryScenario.cs`
- `Client/Scenarios/DrB1FailoverScenario.cs`
- `Client/Scenarios/DrB2FailoverScenario.cs`
- `Client/Scenarios/DrB3RecoveryScenario.cs`
- `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs`
- `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs`
- `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs`
- `Client/Scenarios/DrD1DirectEndpointScenario.cs`
- `Client/Scenarios/DrD2DirectEndpointScenario.cs`
- `Client/Scenarios/DrD3DirectEndpointScenario.cs`
- `Client/Scenarios/DrD4DirectEndpointScenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/DiscoveryApiResult.cs`
- `Client/Support/ScenarioAssert.cs`
- `Server/Registry/`
- `Server/Provider/`
- `Server/Consumer/`
- `Server/Embedded/`
- `Server/Probe/`

### ResilienceLifecycle

필수 분류:

- `Shared/Messages.cs`
- `Client/Scenarios/RlA1ProviderRestartScenario.cs`
- `Client/Scenarios/RlA2ProviderEndpointRemapScenario.cs`
- `Client/Scenarios/RlA3ReconnectStormScenario.cs`
- `Client/Scenarios/RlA4DrainAndGreenEndpointScenario.cs`
- `Client/Scenarios/RlA5ProviderFlappingScenario.cs`
- `Client/Scenarios/RlB1CancellationCleanupScenario.cs`
- `Client/Scenarios/RlB2CrashDuringInflightScenario.cs`
- `Client/Scenarios/RlB3GracefulShutdownScenario.cs`
- `Client/Scenarios/RlB4RuntimeDrainScenario.cs`
- `Client/Scenarios/RlB5DrainInflightScenario.cs`
- `Client/Scenarios/RlB6GrayFaultScenario.cs`
- `Client/Scenarios/RlC1ClientHostLifecycleScenario.cs`
- `Client/Scenarios/RlC2TopologyRecoveryScenario.cs`
- `Client/Scenarios/RlC3NodePauseRecoveryScenario.cs`
- `Client/Scenarios/RlC4RegistryOutageScenario.cs`
- `Client/Scenarios/RlD1HighFanoutScenario.cs`
- `Client/Scenarios/RlD2ObserverFaultScenario.cs`
- `Client/Scenarios/RlD3DispatchErrorEvidenceScenario.cs`
- `Client/Scenarios/RlD4MissingRequestHandlerScenario.cs`
- `Client/Scenarios/RlD5MixedBurstScenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/LifecycleApiResult.cs`
- `Client/Support/ResilienceProcessManager.cs`
- `Client/Support/ScenarioAssert.cs`
- `Client/Support/TopologyEntryResult.cs`
- `Server/Registry/`
- `Server/Provider/`
- `Server/Consumer/`

### RuntimeMonitoring

필수 분류:

- `Shared/Messages.cs`
- `Client/Scenarios/MonA1SocketEventsScenario.cs`
- `Client/Scenarios/MonA2RegistryEventsScenario.cs`
- `Client/Scenarios/MonA3SpotEventsScenario.cs`
- `Client/Scenarios/MonA4AvailabilityTransitionScenario.cs`
- `Client/Scenarios/MonA5FixedKindsScenario.cs`
- `Client/Scenarios/MonB1KindFilterScenario.cs`
- `Client/Scenarios/MonB2RegistrationValidationScenario.cs`
- `Client/Scenarios/MonC1DispatchFailureScenario.cs`
- `Client/Scenarios/MonD1FailureRecoveryScenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/ScenarioAssert.cs`
- `Server/Registry/`
- `Server/Service/`
- `Server/FilteredService/`
- `Server/ThrowingService/`
- `Server/Trigger/`

### SpotService

필수 분류:

- `Shared/Messages.cs`
- `Client/Scenarios/SmA1Scenario.cs`
- `Client/Scenarios/SmA2Scenario.cs`
- `Client/Scenarios/SmA3Scenario.cs`
- `Client/Scenarios/SmA4Scenario.cs`
- `Client/Scenarios/SmA5Scenario.cs`
- `Client/Scenarios/SmA6Scenario.cs`
- `Client/Scenarios/SmA7Scenario.cs`
- `Client/Scenarios/SmA8Scenario.cs`
- `Client/Scenarios/SmB1Scenario.cs`
- `Client/Scenarios/SmB2Scenario.cs`
- `Client/Scenarios/SmB3Scenario.cs`
- `Client/Scenarios/SmB4Scenario.cs`
- `Client/Scenarios/SmB5Scenario.cs`
- `Client/Scenarios/SmB6Scenario.cs`
- `Client/Scenarios/SmB7Scenario.cs`
- `Client/Scenarios/SmB8Scenario.cs`
- `Client/Scenarios/SmC1Scenario.cs`
- `Client/Scenarios/SmC2Scenario.cs`
- `Client/Scenarios/SmC3Scenario.cs`
- `Client/Scenarios/SmC4Scenario.cs`
- `Client/Scenarios/SmD1Scenario.cs`
- `Client/Scenarios/SmD2Scenario.cs`
- `Client/Scenarios/SmD3Scenario.cs`
- `Client/Scenarios/SmD4Scenario.cs`
- `Client/Scenarios/SmD5Scenario.cs`
- `Client/Scenarios/SmD6Scenario.cs`
- `Client/Scenarios/SmD7Scenario.cs`
- `Client/Scenarios/SmD8Scenario.cs`
- `Client/Scenarios/SmD9Scenario.cs`
- `Client/Scenarios/SmD10Scenario.cs`
- `Client/Scenarios/SmD11Scenario.cs`
- `Client/Scenarios/SmD12Scenario.cs`
- `Client/Scenarios/SmD13Scenario.cs`
- `Client/Scenarios/SmD14Scenario.cs`
- `Client/Scenarios/SmE1Scenario.cs`
- `Client/Scenarios/SmE2Scenario.cs`
- `Client/Scenarios/SmE3Scenario.cs`
- `Client/Scenarios/SmE4Scenario.cs`
- `Client/Scenarios/SmF1Scenario.cs`
- `Client/Scenarios/SmF2Scenario.cs`
- `SM-F3`는 공통 e2e와 `.NET feature-map`에 있는 scenario ID다. `.NET`에 별도
  `SmF3Scenario.cs` 파일이 없더라도 Node에서는 `porting-inventory.ko.md`에 `SM-F3` 행을 만들고,
  대응 scenario 파일 또는 명시적 gap을 기록한다.
- `Client/Scenarios/SmF4Scenario.cs`
- `Client/Scenarios/SmG1Scenario.cs`
- `Client/Scenarios/SmG2Scenario.cs`
- `Client/Scenarios/SmG3Scenario.cs`
- `Client/Scenarios/SmG4Scenario.cs`
- `Client/Scenarios/SmQ9Scenario.cs`
- `Client/Support/ClientOptions.cs`
- `Client/Support/ScenarioAssert.cs`
- `Client/Support/SpotLifecycleOrderContext.cs`
- `Server/Registry/`
- `Server/Gateway/`
- `Server/Play/`
- `Server/Session/`
- `Server/MultiNode/`

`SpotService`는 scenario 수와 server role이 많으므로 중간 커밋은 허용하되, 다음 config로 넘어가는
완료 판정은 전체 `SpotService/run_e2e.sh`와 Codex 에이전트 리뷰가 모두 통과한 뒤에만 한다.

### YieldDispatch

필수 분류:

- `Shared/Messages.cs`
- `Client/GlobalUsings.cs`에 해당하는 Node 공통 import 정책이 필요하면 `Client/Support/`에 둔다.
- `Client/Scenarios/ShutdownYieldScenario.cs`
- `Client/Scenarios/YdA1BasicTerminatorScenario.cs`
- `Client/Scenarios/YdA2YieldTerminatorScenario.cs`
- `Client/Scenarios/YdA3ContinuationContextScenario.cs`
- `Client/Scenarios/YdA4WorkerYieldScenario.cs`
- `Client/Scenarios/YdB1OtherActorProgressScenario.cs`
- `Client/Scenarios/YdB2SameActorReentryScenario.cs`
- `Client/Scenarios/YdB3ActorJoinYieldScenario.cs`
- `Client/Scenarios/YdC1TimerIsolationScenario.cs`
- `Client/Scenarios/YdC2TimerReentryScenario.cs`
- `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs`
- `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs`
- `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs`
- `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs`
- `Client/Scenarios/YdE1TimeoutScenario.cs`
- `Client/Scenarios/YdE2CancellationScenario.cs`
- `Client/Scenarios/YieldActorScenarioContext.cs`는 scenario ID가 없는 context 파일이다. Node에서는
  `Client/Support/` 또는 `Shared/`로 재분류한다.
- `Client/Support/ClientOptions.cs`
- `Client/Support/ScenarioAssert.cs`
- `Server/Registry/`
- `Server/Play/`
- `Server/Session/`
- `Server/Delay/`

`YieldDispatch`는 Node public surface에 필요한 기능이 없을 가능성이 높다. public contract 근거가 없는
기능은 구현하지 않고 gap으로 기록한다. contract 근거가 있는데 Node public API가 빠져 있으면 별도
public contract parity 작업으로 분리한다.

## 누락 방지 체크리스트

각 config 완료 전에 아래 항목을 모두 확인한다.

- `.NET` 기준 파일 목록과 Node 파일 목록을 나란히 비교했다.
- `porting-inventory.ko.md`에 `.NET` 기준 파일이 모두 있고 `pending` 상태가 없다.
- `bin/`, `obj/`, `logs/`를 제외한 `.NET` source 파일의 책임이 Node에 모두 대응된다.
- 공통 e2e 문서의 scenario ID가 Node `Client/Scenarios/` 파일과 `feature-map.ko.md`에 모두 나타난다.
- `.NET feature-map.ko.md`의 부분 구현/gap 항목을 완료로 과장하지 않았다.
- `.NET role root의 option/endpoint/handler/evidence/support 파일을 목표 폴더로 재분류했다.
- scenario ID가 없는 helper/context 파일을 `Client/Scenarios/`에 두지 않았다.
- `Server/<Role>/` 수와 이름이 `.NET`과 같은 의미로 대응된다.
- `Shared/messages.ts`가 server와 client의 중복 DTO 생성을 막는다.
- `run_e2e.sh`가 실패 시 최근 로그를 출력한다.
- `run_e2e.sh`가 정상 종료와 실패 종료 모두에서 child process를 정리한다.
- Node package build가 root package build와 충돌하지 않는다.
- Node implementation이 private/internal API, reflection 우회, raw frame 조작, test-only adapter를 쓰지 않는다.
- 버그를 발견했을 때 scenario를 통과시키는 임시 우회 대신 원인을 수정하고 회귀 테스트를 추가했다.
- scenario 실행을 대신하는 driver server, scenario runner server, `/run all` endpoint가 없다.
- feature-map에 "완료"라고 쓴 항목은 실제 실행 로그와 marker로 증명된다.
- public contract gap은 완료로 표시하지 않는다.
- Codex 에이전트 리뷰가 이슈 없음으로 끝났다.

## 커밋 규칙

작업트리가 더러울 수 있으므로 커밋은 항상 config 단위로 좁힌다.

- 한 config를 완전히 끝내기 전에는 다른 config 파일을 수정하지 않는다.
- 커밋 pathspec은 `framework/languages/node/e2e/<Config>`와 필요한 공통 Node build 설정에만 제한한다.
- 문서 수정이 필요하면 같은 config 완료 커밋에 포함하거나 별도 문서 커밋으로 분리한다.
- unrelated 변경, 다른 언어 e2e 변경, generated artifact, 로그 파일은 커밋하지 않는다.
- 최종 push 전에는 `git diff --cached --name-status`로 staged 범위를 확인한다.

## 중단 조건

아래 상황이 나오면 해당 config 작업을 멈추고 설계 이슈로 분리한다.

- `.NET` scenario가 Node public contract에 없는 기능을 요구한다.
- 기능을 맞추려면 private API, raw frame, test-only adapter, framework 내부 helper가 필요하다.
- Node와 `.NET`의 public 동작 차이가 사용자에게 보이지만 spec/guide에 어느 쪽이 맞는지 근거가 없다.
- 같은 role을 분리하지 않고 하나의 server에 mode 분기로 넣어야만 통과할 것처럼 보인다.
- `run_e2e.sh`가 실제 프로세스 경계 없이 in-process contract test처럼 바뀐다.

중단한 항목은 `feature-map.ko.md`와 별도 follow-up 문서에 기록한다. 중단 항목을 완료로 표시하지 않는다.
