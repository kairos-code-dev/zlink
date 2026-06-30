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
새 public API가 필요하면 먼저 문서 검토 대상으로 분리한다. core 공개 API 계약이면 `doc/spec/draft/`
아래에서 초안을 다루고, Node framework 공개 계약이면 `framework/doc/framework/node/spec/`와
`framework/doc/framework/node/guide/` 아래 문서에서 계약과 사용법을 분리해 검토한다.

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
확장한다. 아래 표의 완료 조건은 최종 목표 기준이다. 현재 checkout처럼 public contract gap이 남은 상태에서
구현 가능한 범위만 통과했다면, 해당 config는 완료가 아니라 `현재 진행 상태`와 `후속 gap 정리`에 이월한다.

| 순서 | Config | 기준 문서 | 최종 목표 조건 |
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

## 현재 진행 상태

아래 표는 2026-06-30 현재 checkout에서 확인한 Node 포팅 상태다. `runner proof`는 구현된 범위가 실제
`run_e2e.sh`로 통과한 로그를 뜻한다. `gap/partial`은 완료가 아니라 후속 public contract parity 작업의
입력이다.

| Config | 현재 판정 | runner proof | gap/partial |
|--------|-----------|--------------|-------------|
| `RegistryMessaging` | full 구현 범위 통과 | `logs/20260630-080014-3192638` | 없음 |
| `PubSub` | 구현 범위 통과 | `logs/20260630-093248-3426974` | 없음 |
| `RegistrationCodec` | 구현 범위 통과 | `logs/20260630-102915-3514768` | 없음 |
| `DiscoveryRegistryHa` | 구현 범위 통과 | scenario별 실행 로그는 아래 상세 목록 참고 | 없음 |
| `ResilienceLifecycle` | 구현 범위 통과 | `logs/20260630-061338-2889689` | 없음 |
| `RuntimeMonitoring` | 구현 범위 통과 | `logs/20260630-064526-2998934` | 없음 |
| `SpotService` | default `all` 구현 범위 통과, routed spot request, publish-only delivery, stream TLS 구현 범위 통과 | `logs/20260630-101424-3467655`; `SM-C4` 선택 proof `logs/20260630-083734-3303700`; `SM-D14` 선택 proof `logs/20260630-085904-3356699`; `SM-F3` 선택 proof `logs/20260630-091213-3386438`; `SM-F4` 선택 proof `logs/20260630-101412-3466073`; `SM-F5` 선택 proof `logs/20260630-091846-3399628`; 추가 선택 proof는 config 문서의 scenario별 로그 참고 | 없음 |
| `YieldDispatch` | full 구현 범위 통과, cross-language aggregation 입력 준비 | `logs/20260630-102243-3490701` | 없음 |

`DiscoveryRegistryHa` runner는 현재 `all` 인자를 제공하지 않으므로 지원 scenario를 개별 실행했다. 각 로그의
`client.stdout.log`에는 해당 scenario marker와 `discovery-registry-ha e2e result=passed`가 함께 남아 있다.

| Scenario | proof log |
|----------|-----------|
| `DR-A1` | `logs/20260630-022102-2343678` |
| `DR-A2` | `logs/20260630-022108-2344016` |
| `DR-A3` | `logs/20260630-022113-2344464` |
| `DR-A4` | `logs/20260630-022120-2344980` |
| `DR-B1` | `logs/20260630-022134-2345758` |
| `DR-B2` | `logs/20260630-043800-2668198` |
| `DR-B3` | `logs/20260630-022140-2346239` |
| `DR-C1` | `logs/20260630-022148-2346991` |
| `DR-C2` | `logs/20260630-022151-2347457` |
| `DR-C3` | `logs/20260630-022157-2347999` |
| `DR-D1` | `logs/20260630-022206-2348746` |
| `DR-D2` | `logs/20260630-022211-2349015` |
| `DR-D3` | `logs/20260630-022216-2349345` |
| `DR-D4` | `logs/20260630-022220-2349869` |

## 후속 gap 정리

현재 Node E2E plan 안에 남은 후속 gap ID는 없다. 새 gap을 발견하면 public spec, guide, draft 검토를 거쳐
계약으로 받아들일지 먼저 정해야 한다. 계약 근거가 없는 기능은 e2e를 통과시키기 위해 바로 public API로 추가하지 않는다.
후속 작업을 시작할 때는 `framework/doc/framework/node/internals/dotnet-to-node-surface-mapping.ko.md`와
`framework/doc/framework/node/internals/node-binding-public-api-gap-list.ko.md`를 먼저 갱신 대상으로 확인한다.
core 공개 API 계약 초안은 `doc/spec/draft/` 아래에서 다루고, Node framework 공개 계약 초안은
`framework/doc/` 아래의 Node spec/guide/plan 문서로 분리한다. 어느 경우에도 확정되지 않은 계약을 기존
정식 spec에 바로 섞지 않는다.

gap을 기록할 때는 계층을 먼저 나눈다. `node-binding-public-api-gap-list.ko.md`의 현재 판정은 binding
entry point와 backend adapter에 필요한 공개 API가 충분하다는 뜻이지, Node framework builder/runtime
표면의 모든 parity gap이 해결됐다는 뜻이 아니다. 후속 gap이 `ZLinkModule`, `zlinkFramework()`, handler
contract, runtime lifecycle 같은 framework 표면 문제라면 Node framework spec/guide와 config 문서에
기록한다. 실제로 `@zlink-systems/zlink` binding 공개 API가 부족한 경우에만 binding gap 문서를 갱신한다.

여러 framework 언어의 Config 8 report를 모아 비교하는 aggregation은 이 Node plan의 남은 gap이 아니라
별도 cross-language parity gate 입력이다. Node `YieldDispatch` report는 공통 scenario id와 marker 이름을
사용하며 `logs/20260630-102243-3490701`에서 `yield-dispatch e2e result=passed`를 확인했다.

### 후속 gap 작업 시작 체크리스트

후속 gap을 실제로 닫을 때는 아래 순서를 먼저 끝낸 뒤 구현에 들어간다. 이 순서를 지키지 않으면 공통 E2E나
다른 언어 구현만 보고 Node public API를 추가하는 실수를 할 수 있다.

1. 대상 ID가 적힌 `feature-map.ko.md`와 `porting-inventory.ko.md` 행을 읽고, 현재 판정이 public contract
   gap인지 harness gap인지 다시 확인한다.
2. 같은 scenario의 `.NET` `feature-map.ko.md`를 읽고, `.NET`도 완료인지 부분 구현인지 확인한다. `.NET`에서
   부분 구현인 항목은 Node에서 완료 기준으로 승격하지 않는다.
3. 공통 E2E 문서와 framework 공통 spec/guide를 대조해, 새 public API 없이 기존 계약으로 닫을 수 있는지
   확인한다.
4. 기존 계약으로 닫을 수 없으면 새 public contract 후보로 분리한다. core 공개 API 후보는 `doc/spec/draft/`
   아래 draft로, Node framework 후보는 `framework/doc/framework/node/spec/`와
   `framework/doc/framework/node/guide/` 아래 문서로 나누어 검토한다.
5. 계약으로 받아들이면 public API, runtime 연결, config E2E, feature map, inventory, runner proof를 한
   묶음으로 갱신한다. 받아들이지 않으면 해당 config 문서에 제외 사유와 대체 계획을 남기고 완료로 표시하지
   않는다.
6. 마지막에 해당 config의 실제 `run_e2e.sh`와 READ-ONLY 리뷰를 다시 실행한다. 리뷰에서 issue가 나오면 같은
   gap 묶음 안에서 수정, 재실행, 재리뷰를 반복한다.

우선순위는 여러 config를 함께 막는 항목부터 뒀다. channel socket runtime option 묶음은
`RegistryMessaging`의 `RM-C9` proof로 닫았다. 이후 SpotService public contract 항목,
ResilienceLifecycle의 runtime drain/in-flight 항목, RuntimeMonitoring의 drain availability 항목도 구현 proof로 닫았다.

첫 작업 묶음에서는 weight, HWM, drain/restore, admission event를 framework 공통 계약으로 받을지 먼저
결정했고, HWM/send-timeout backpressure는 Node public builder와 runtime nonblocking submitter 연결로 닫았다.
두 번째 작업 묶음은 SpotService였다. 이 항목은
한 덩어리로 처리하지 않고 same-node/target routed spot request 결과, publish marker와 error
reply, publish-only delivery, stream TLS server option으로 나누어 재현 로그와 새 pass 로그를 각각
대조한다. 세 번째 작업 묶음에서는 PubSub 검증 경로와 Codec/DI lifecycle을 정리한다. 마지막으로
YieldDispatch report readiness처럼 단일 표면에 가까운 항목을 별도 작업으로 닫았다.

### 첫 작업 묶음 시작 입력

Channel socket runtime option 묶음은 아래 판정으로 닫았다. 이 묶음은 runner만 통과시켜 완료로 표시하지
않고, public builder 설정과 runtime nonblocking submitter 연결이 함께 검증됐을 때 완료로 올렸다.

| 확인 항목 | 현재 확인한 입력 | 후속 작업에서 남길 판정 |
|-----------|------------------|-------------------------|
| `.NET` 기준 상태 | `.NET` feature map에서 `RM-C9`는 구현 상태다. | Node가 따라야 할 동작인지, 또는 `.NET` 전용/추가 설계 후보인지 명시한다. |
| Node 현재 상태 | Node feature map과 inventory는 `RM-C9`를 구현 상태로 기록한다. | 기존 public API로 닫았고 새 public API는 추가하지 않았다. |
| 공통 계약 근거 | `framework-api.ko.md`는 backpressure 처리와 send timeout 정책을 다루지만, weight와 runtime drain/restore를 Node builder 계약으로 바로 추가하라는 근거는 아니다. | weight, HWM, send timeout, drain/restore, admission event 중 어떤 항목을 공통 계약으로 받을지 분리해 판정한다. |
| Node 문서 위치 | Node channel spec과 `dotnet-to-node-surface-mapping.ko.md`가 public builder/runtime 표면을 확인할 1차 문서다. | 계약 채택 시 Node spec/guide와 surface mapping을 함께 갱신하고, 미채택 시 제외 사유를 config 문서에 남긴다. |

이 묶음의 산출물은 하나의 결론 문장으로 끝내지 않는다. 최소한 `RM-C9`에 대해
계약 채택 여부, 필요한 public API 이름 또는 제외 사유, 다시 실행한 runner,
새 proof 로그 위치를 남겼다.

### Channel socket runtime option 판정 기록

현재 checkout에서 첫 작업 묶음의 계약 근거를 다시 대조했다. 공통 framework spec은 send backpressure를
nonblocking submit, pending queue, ready notification, `SendTimeout` 정책으로 설명한다. Node public
surface는 startup server weight를 `configureServerSocket().weight`로 제공하고,
`configureClientSocket()`으로 client socket HWM과 send timeout을 설정할 수 있다. public
`ZLinkChannelRuntimeOptions`로 runtime weight drain/restore도 수행할 수 있다.

| 대상 ID | 계약 판정 | 근거 문서 | 구현 범위와 proof |
|---------|-----------|-----------|-------------------|
| `RM-C7` | 구현 | 공통 E2E는 server weight 분산을 요구하고 Node public builder가 `configureServerSocket().weight`를 제공한다. | `RegistryMessaging/run_e2e.sh all` 로그 `logs/20260630-080014-3192638`에서 `scenario RM-C7 passed`를 확인했다. |
| `RM-C9` | 구현 | 공통 spec은 send timeout과 async submitter backpressure 정책을 설명하고, 공통 E2E는 HWM 포화 관찰을 요구한다. Node public builder는 client socket HWM/send-timeout 설정을 제공한다. | `RegistryMessaging/run_e2e.sh all` 로그 `logs/20260630-080014-3192638`에서 `scenario RM-C9 passed`를 확인했다. 32개 slow send 입력에서 bounded failure와 recovery marker를 확인했다. |
| `RL-A4` | 구현 | public `ZLinkChannelRuntimeOptions`로 runtime drain을 걸고 green provider 전환과 original provider same-endpoint restore 뒤 routing 복귀를 검증한다. | `ResilienceLifecycle/run_e2e.sh all` 로그 `logs/20260630-061338-2889689`에서 `scenario RL-A4 passed`를 확인했다. |
| `RL-B4` | 구현 | public `ZLinkChannelRuntimeOptions`와 provider admin path로 runtime drain/restore를 만든다. | `ResilienceLifecycle/run_e2e.sh all` 로그 `logs/20260630-061338-2889689`에서 `scenario RL-B4 passed`를 확인했다. |
| `RL-B5` | 구현 | public `ZLinkChannelRuntimeOptions` runtime drain 중 새 request만 차단하고, drain 전에 시작된 slow request의 completion evidence와 reply를 보존한다. | `ResilienceLifecycle/run_e2e.sh all` 로그 `logs/20260630-061338-2889689`에서 `scenario RL-B5 passed`를 확인했다. |
| `MON-A4` | 구현 | public `ZLinkChannelRuntimeOptions`로 service server socket weight를 drain/restore하고 trigger client socket의 `PeerAdmissionChanged` evidence, service admin evidence, registry topology evidence를 검증한다. | `RuntimeMonitoring/run_e2e.sh all` 로그 `logs/20260630-064526-2998934`에서 `scenario MON-A4 passed`를 확인했다. |

따라서 이 묶음에서 남은 `RegistryMessaging` config gap은 없다. 이번 판정은 raw binding option이나
internal runtime option으로 marker만 맞추지 않고, public `configureClientSocket()` 설정과 runtime
nonblocking submitter 연결로 HWM 포화가 실제로 관찰될 때 완료로 올린다는 기록이다. weight/drain의 선행
설계는 `framework/doc/plan/framework-channel-drain-peer-weight-plan.ko.md`가 이미 소유한다.

### SpotService 작업 묶음 시작 입력

SpotService gap은 한 번에 닫지 않고 원인별로 나누어 처리했다. 각 하위 묶음마다 실패 재현 로그와 새 pass
로그를 따로 남겼고, default `all` runner와 선택 scenario proof를 함께 확인했다.

| 하위 묶음 | 대상 ID | 현재 확인한 입력 | 후속 작업에서 남길 판정 |
|-----------|---------|------------------|-------------------------|
| routed spot request | `SM-A2`, `SM-A3`, `SM-A4`, `SM-A5`, `SM-C1`, `SM-C3`, `SM-E1`, `SM-E3`, `SM-F1`, `SM-F2`, `SM-G2`, `sm-q9` | route bridge owner의 same-node local dispatch 보강 뒤 선택 scenario로 통과했다. | config 문서에 남긴 PASS 로그와 default `all` proof를 함께 유지한다. |
| stream TLS server | `SM-D14` | Node framework/Nest stream node builder가 public `setTlsServer(...)`로 server certificate/key를 받고, stream connector가 strict validation 실패와 skip-validation 성공을 검증했다. | 구현 proof를 config 문서에 유지한다. |
| channel socket ownership | `SM-F5` | 공통 E2E는 같은 RouteMesh channel로 일반 request와 target spot route를 처리한 뒤 spot route 사용/중단이 일반 channel socket을 흔들지 않는지 요구한다. | public spot close 뒤 같은 RouteMesh 일반 request가 계속 성공하는지 검증한다. |

각 하위 묶음은 `framework/doc/framework/common/e2e/config-2-spot-service.ko.md`와 Node spot/actor/stream
spec을 먼저 대조한다. 새 public API가 필요하면 Node framework spec/guide 또는 draft로 분리하고, 내부 relay
packet이나 raw-frame 주입으로 scenario만 통과시키지 않는다.

### SpotService 판정 기록

현재 checkout에서 SpotService gap의 계약 근거를 다시 대조했다. 공통 E2E는 route mesh 기반 spot route,
publish-only SpotMesh, stream TLS를 검증 대상으로 둔다. Node spec은 current Spot callback
안의 `context.outbound.sendToSpot(...)`/`requestToSpot(...)`, local spot 없는 노드의
`ZLinkSpotPublisherClient.publishSpot(...)`를 공개 표면으로 설명한다. public `ZLinkRouteClient`는
`send(...)`/`request(...)`만 노출하고 target spot으로 직접 보내는 메서드를 공개하지 않는다. Node stream
node builder는 public `setTlsServer(...)`로 TLS server certificate/key를 받는다.

| 하위 묶음 | 대상 ID | 계약 판정 | 구현 범위와 proof |
|-----------|---------|-----------|-------------------|
| routed spot request | `SM-A2`, `SM-A3`, `SM-A4`, `SM-A5`, `SM-C1`, `SM-C3`, `SM-E1`, `SM-E3`, `SM-G2`, `sm-q9` | 구현 | Spot context outbound와 resolver 기반 route는 기존 public contract로 검증했다. 선택 PASS 로그는 config 문서에 남겼다. |
| route client target spot | `SM-F1`, `SM-F2`, `SM-F3`, `SM-F4`, `SM-F5` | 구현 | `SM-F1`/`SM-F2` target spot request/command, `SM-F3` same RouteMesh 일반 request와 target spot route 혼재, `SM-F4` missing target request/send drop evidence, `SM-F5` spot close 뒤 same RouteMesh 일반 channel request 생존을 public route-client와 spot 관리 표면으로 검증했다. `SM-F3` 선택 PASS: `logs/20260630-091213-3386438`; `SM-F4` 선택 PASS: `logs/20260630-101412-3466073`; `SM-F5` 선택 PASS: `logs/20260630-091846-3399628`. |
| stream TLS server | `SM-D14` | 구현 | public `setTlsServer(...)`와 stream connector TLS validation option으로 strict validation failure와 skip-validation auth/request/push success를 검증했다. 선택 PASS: `logs/20260630-085904-3356699`; `all` PASS: `logs/20260630-101424-3467655`. |

이 판정으로 SpotService gap을 닫았다. public API를 바꾸지 않고 runtime 경로와 scenario를 다시 검증했으며,
draft 후보였던 항목은 spec/guide 갱신 없이 public API로 추가하지 않았다.

### PubSub와 Codec 작업 묶음 시작 입력

PubSub와 RegistrationCodec은 같은 세 번째 작업 묶음에 있지만 성격이 다르다. PubSub는 delivery 동작 자체보다
검증 경로 계약을 명확히 해야 했고, RegistrationCodec은 DI lifecycle 계약을 Node/Nest public surface에 맞춰
정리해야 했다. 그래서 두 config를 같은 runner 통과 여부로 묶어 완료 처리하지 않는다.

| 하위 묶음 | 대상 ID | 현재 확인한 입력 | 후속 작업에서 남길 판정 |
|-----------|---------|------------------|-------------------------|
| PubSub 검증 경로 | `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1` | PubSub config는 client가 publish를 트리거하고 subscriber 역할 server의 evidence를 조회한다고 설명한다. 공통 E2E README는 Pub/Sub fanout처럼 수신자가 subscriber 역할 server인 경우 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고 정리했다. | 별도 client stream connector observer 없이 실제 subscriber 역할 server evidence를 완료 proof로 사용한다. |
| DI lifecycle | `RC-A4` | `.NET`은 dispatch별 async scope와 dispose marker를 구현했다. Node는 public `ContextIdFactory`, `ModuleRef.resolve(...)`, `registerRequestByContextId(...)` 경로로 per-dispatch scoped id 분리와 singleton 안정성을 검증했다. Nest public API는 dispatch context dispose 표면을 제공하지 않는다. | Node/Nest 완료 조건은 scoped id 분리와 singleton 안정성으로 둔다. 내부 wrapper 저장소 삭제나 테스트 전용 adapter로 dispose counter를 맞추지 않는다. |

PubSub 항목은 공통 README, `.NET` feature map, Node feature map, Node inventory를 함께 정리해 구현으로
승격했다. RegistrationCodec의 DI lifecycle은 Node/Nest public surface 범위에서 구현으로 판정했다. 내부 probe나
테스트 전용 adapter로 dispose counter를 맞추지 않는다.

### PubSub와 Codec 판정 기록

현재 checkout에서 PubSub와 RegistrationCodec gap의 계약 근거를 다시 대조했다. PubSub config 문서는
client가 publish를 트리거하고 subscriber evidence를 조회한다고 설명한다. 공통 E2E README도 Pub/Sub
fanout처럼 event 수신자가 client stream session이 아니라 subscriber 역할 server인 경우에는 subscriber
handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고 정리했다. 따라서 PubSub의
대상 항목은 `.NET`과 Node 모두 별도 client stream connector observer 없이 구현으로 판정한다.

RegistrationCodec은 성격이 다르다. `.NET`은 `RC-A4`, `RC-B2`, `RC-B4`를 구현했다. Node는 `RC-B2`
content-type을 공통 `application/x-protobuf`로 맞췄고, `RC-B4`는 serializer별 `canSerialize` public
predicate로 JSON/Protobuf/MessagePack 공존을 검증했다. 최신 proof는
`logs/20260630-102915-3514768`이다. 이 로그에서 `RC-A4`는 marker가 통과하고 singleton id는 유지되며
scoped id는 dispatch마다 달라진다.
Node handler dispatch는 NestJS context별 provider resolve를 사용해 request마다 다른 scoped id를 남긴다.
현재 설치된 Nest public export는 `ContextIdFactory`, `ModuleRef.resolve(...)`,
`registerRequestByContextId(...)`를 제공하지만, `ContextIdFactory.create()`로 만든 request context를
dispatch 뒤 명시적으로 해제하는 API는 제공하지 않는다. 그래서 내부 wrapper 저장소 삭제나 테스트 전용
adapter로 dispose counter를 맞추지 않는다. Node/Nest의 public 완료 조건은 scoped id 분리와 singleton 안정성이다.

| 하위 묶음 | 대상 ID | 계약 판정 | 구현 범위와 proof |
|-----------|---------|-----------|-------------------|
| PubSub 검증 경로 | `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1` | 구현 | 실제 subscriber 역할 server의 bounded `/evidence/wait` marker를 성공 기준으로 사용한다. runner proof는 `logs/20260630-093248-3426974`이며 `pubsub e2e result=passed`를 확인했다. |
| DI lifecycle | `RC-A4` | 구현 | per-dispatch scope evidence는 public dispatch 경로로 통과했다. Node/Nest public API에는 dispatch context 해제 표면이 없으므로 내부 lifecycle probe나 Nest 내부 wrapper 삭제 없이 scoped id 분리와 singleton 안정성을 완료 조건으로 둔다. runner: `RegistrationCodec/run_e2e.sh`. |

이 판정에서 `PubSub`와 `RegistrationCodec`은 구현 proof로 닫았다.

### 단일 표면 작업 묶음 시작 입력

Monitoring, YieldDispatch의 남았던 항목은 서로 다른 표면을 건드리므로 한 작업으로 묶어 닫지
않았다. 각 항목은 먼저 공통 E2E 문서와 Node spec을 대조한 뒤, 계약 채택 여부와 새 proof 범위를 따로
남겼다.

| 하위 묶음 | 대상 ID | 현재 확인한 입력 | 후속 작업에서 남길 판정 |
|-----------|---------|------------------|-------------------------|
| Yield cross-language report | `YD-E5` | Node와 `.NET` 모두 공통 scenario id와 marker 이름을 유지한다. | Node config 완료 조건은 report readiness다. 여러 framework 언어 report aggregation은 별도 cross-language parity gate로 분리했다. |

YieldDispatch의 `YD-A3`는 공통 문서 요구를 public Spot request handler 표면에 맞춰 좁힌 뒤 구현으로 판정했다.
`YD-E5`는 Node report readiness로 닫았고, 여러 언어 report를 한 번에 비교하는 단계는 이 plan 바깥의
cross-language parity gate 입력으로 남긴다.
Monitoring `MON-A5`는 native disconnect reason이 handshake failure인 경우를 public `HandshakeFailed`
kind로 매핑해 구현 proof를 확보했다.

### 단일 표면 판정 기록

현재 checkout에서 남은 단일 표면 gap을 다시 대조했다. Discovery `DR-B2`는 public multi-endpoint harness와
runtime client 준비 시점 보정 뒤 `logs/20260630-043800-2668198`에서 통과했다. consumer는 살아 있는 registry
endpoint가 포함된 configured endpoint 목록으로 provider discovery와 messaging을 bounded timeout 안에
완료한다.

Monitoring `MON-A5`는 Node public enum에 `HandshakeFailed`와 `Internal`이 있고, native
`Disconnected(value=3)`을 handshake failure reason으로 해석해 public `HandshakeFailed` kind로 매핑한다.
`timeout 420s ./run_e2e.sh MON-A5` targeted run `logs/20260630-064446-2996779`와
`timeout 720s ./run_e2e.sh all` run `logs/20260630-064526-2998934`에서 `scenario MON-A5 passed`를 확인했다.

YieldDispatch `YD-A3`는 request id, spot rid, correlation id, continuation marker order를 public E2E 완료 범위로 정리했다.
stream metadata 직접 노출은 Spot request handler public surface가 아니므로 완료 조건에서 제외했다.
`YD-E5`는 Node report가 공통 scenario id와 marker 이름을 쓰는지 확인하는 범위에서는 완료했다. 여러 framework
언어 report를 한 번에 모아 비교하는 cross-language aggregation은 이 Node config의 남은 gap이 아니라 별도
parity gate 입력이다. 현재 live checkout에서 Java는 `YD-E1`~`YD-E4`가 gap이고, C++는 Track A(`YD-A1`~`YD-A4`)까지만
runner proof가 있으므로 aggregation gate를 만들더라도 성공 gate가 아니라 남은 언어별 coverage를 드러내는
실패/준비 gate가 된다.

| 하위 묶음 | 대상 ID | 계약 판정 | 구현 범위와 proof |
|-----------|---------|-----------|-------------------|
| Yield cross-language report | `YD-E5` | 구현 | Node report는 공통 scenario id와 marker 이름을 사용한다. runner proof는 `logs/20260630-102243-3490701`이며 cross-language aggregation은 별도 parity gate 입력으로 분리했다. |

이 판정에서 `MON-A5`, `YD-A3`, `YD-E5`는 구현 proof로 닫았다. cross-language aggregation gate는 Node
config 완료 조건이 아니라 여러 언어 report가 준비된 뒤 실행할 별도 검증 단계다.

### Gap 해소 기록 형식

후속 gap을 닫거나 제외할 때는 대상 ID마다 같은 형식으로 기록한다. 이 형식은 별도 문서가 아니라 각 config의
`feature-map.ko.md`와 `porting-inventory.ko.md`, 필요한 spec/guide/draft 문서에 같은 의미로 반영한다.

| 기록 필드 | 내용 |
|-----------|------|
| 대상 ID | 예: `RM-C7`, `SM-A2`, `YD-E5`. 여러 ID를 한 줄에 합치지 않는다. |
| 계약 판정 | 기존 public contract로 가능, 새 public contract 채택, draft 후보, 제외 중 하나를 명시한다. |
| 근거 문서 | 공통 E2E, framework 공통 spec/guide, Node spec/guide, draft 중 실제로 확인한 문서 경로를 적는다. |
| 구현 범위 | public API, runtime 연결, scenario, runner 중 무엇을 바꿨는지 적는다. 제외라면 구현하지 않은 이유와 대체 계획을 적는다. |
| proof | 새 로그 디렉터리, scenario marker, 최종 `<config> e2e result=passed` marker를 적는다. 선택 scenario라면 default `all` 포함 여부도 적는다. |
| review | READ-ONLY 리뷰 결과를 적는다. substantive issue가 있으면 완료가 아니라 수정 대기 상태로 남긴다. |

이 기록이 없는 ID는 marker가 있어도 완료로 보지 않는다. 반대로 제외 판정을 받은 ID는 제외 사유와 대체 계획이
있어야 하며, 단순히 runner에서 빼는 방식으로 완료 처리하지 않는다.

후속 작업은 아래 산출물을 함께 갱신해야 한다. 계약 문서만 바꾸거나 e2e만 통과시키는 상태는 완료로 보지
않는다.

| Gap 묶음 | 먼저 확인할 계약 문서 | 함께 갱신할 config 문서 | 완료 proof |
|----------|----------------------|-------------------------|------------|
| Channel socket runtime option과 backpressure | `framework/doc/framework/common/spec/framework-api.ko.md`, `framework/doc/framework/node/spec/nestjs-channel-messaging.ko.md`, `framework/doc/framework/node/internals/dotnet-to-node-surface-mapping.ko.md` | `RegistryMessaging/feature-map.ko.md`, `RegistryMessaging/porting-inventory.ko.md` | 계약 채택 여부가 적힌 문서 diff, 채택 시 Node public API/runtime test, `RegistryMessaging/run_e2e.sh` 재실행 로그와 `RM-C9` marker |
| DI lifecycle 표면 | `framework/doc/framework/node/spec/handler-interfaces.ko.md`, `framework/doc/framework/node/internals/di-capability-exposure-policy.ko.md` | `RegistrationCodec/feature-map.ko.md`, `RegistrationCodec/porting-inventory.ko.md` | `RC-A4` scoped id evidence와 `registration-codec e2e result=passed` |
| Discovery multi-endpoint 동작 | `framework/doc/framework/node/spec/nestjs-registry.ko.md`, `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md` | `DiscoveryRegistryHa/feature-map.ko.md`, `DiscoveryRegistryHa/porting-inventory.ko.md`, 필요하면 `run_e2e.sh`의 `all` 지원 여부 | `DR-B2` marker와 `discovery-registry-ha e2e result=passed` |
| Monitoring fixed-kind coverage | `framework/doc/framework/node/spec/nestjs-monitoring.ko.md` | `RuntimeMonitoring/feature-map.ko.md`, `RuntimeMonitoring/porting-inventory.ko.md` | `MON-A5` marker와 `runtime-monitoring e2e result=passed` |
| Spot route, publish, stream server | `framework/doc/framework/node/spec/nestjs-spot.ko.md`, `framework/doc/framework/node/spec/nestjs-stream.ko.md` | `SpotService/feature-map.ko.md`, `SpotService/porting-inventory.ko.md` | `후속 gap 정리`의 SpotService 대상 ID marker, 보충 operation `sm-q9` marker, `spot-service e2e result=passed`, 실패 재현 로그가 새 pass 로그로 대체된 증거 |
| YieldDispatch report readiness | `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`, `framework/doc/framework/node/spec/nestjs-spot.ko.md`, `framework/doc/framework/node/spec/handler-interfaces.ko.md`, `framework/doc/framework/node/guide/05-spot.ko.md` | `YieldDispatch/feature-map.ko.md`, `YieldDispatch/porting-inventory.ko.md` | `YD-E5` Node report readiness, `yield-dispatch e2e result=passed`; cross-language aggregation은 별도 parity gate 입력 |

## 현재 checkout 상태 검증 게이트

현재 checkout의 runner proof는 구현 가능한 범위가 통과했다는 증거다. 아래 항목을 모두 확인하기 전에는
Node E2E 포팅 완료로 표시하지 않는다.

1. `후속 gap 정리` 항목이 있으면 각 항목을 public contract로 받아들일지 결정한다.
2. 계약으로 받아들인 항목은 Node framework public API와 e2e scenario로 구현하고, 받아들이지 않은 항목은
   `feature-map.ko.md`와 관련 spec/guide에 제외 사유와 대체 계획을 남긴다.
3. 각 config의 `feature-map.ko.md`와 `porting-inventory.ko.md`에서 완료, 부분 구현, gap 상태가 같은 뜻으로
   맞는지 다시 대조한다.
4. gap 해결 뒤 해당 config의 실제 `run_e2e.sh`를 다시 실행하고, 새 로그의 scenario marker와 최종
   `<config> e2e result=passed` marker를 `feature-map.ko.md`에 반영한다.
5. config별 READ-ONLY 리뷰를 다시 실행한다. 리뷰에서 substantive issue가 나오면 같은 config 안에서 수정,
   재실행, 재리뷰를 반복한다.

아래 명령은 현재 checkout에서 문서와 runner 증거가 서로 맞는지 확인하는 상태 검증용이다. 이 스크립트가
통과해도 `후속 gap 정리` 항목이 남아 있으면 완료가 아니다. `rg`는 로그 디렉토리가 ignore 대상일 수
있으므로 proof marker 확인에는 `--no-ignore`를 붙인다.

```bash
node_e2e_configs=(
  RegistryMessaging PubSub RegistrationCodec DiscoveryRegistryHa
  ResilienceLifecycle RuntimeMonitoring SpotService YieldDispatch
)
[[ "${#node_e2e_configs[@]}" == 8 ]] || {
  printf 'node_e2e_configs must contain 8 configs, count=%s\n' "${#node_e2e_configs[@]}" >&2
  exit 1
}
plan_file=framework/doc/plan/framework-node-e2e-dotnet-porting-plan.ko.md
required_plan_sections=(
  '## 진행 순서'
  '## 현재 진행 상태'
  '## 후속 gap 정리'
  '### 후속 gap 작업 시작 체크리스트'
  '### 첫 작업 묶음 시작 입력'
  '### SpotService 작업 묶음 시작 입력'
  '### PubSub와 Codec 작업 묶음 시작 입력'
  '### 단일 표면 작업 묶음 시작 입력'
  '### Gap 해소 기록 형식'
  '## 현재 checkout 상태 검증 게이트'
)
for heading in "${required_plan_sections[@]}"; do
  count="$(rg --no-ignore -x -F "$heading" "$plan_file" | wc -l)"
  [[ "$count" == 1 ]] || {
    printf 'required plan section must appear exactly once: %s count=%s\n' "$heading" "$count" >&2
    exit 1
  }
done

node_e2e_doc_files=()
node_e2e_inventory_files=()
node_e2e_runner_files=()
required_node_e2e_files=()
for config in "${node_e2e_configs[@]}"; do
  config_dir="framework/languages/node/e2e/$config"
  node_e2e_doc_files+=("$config_dir/feature-map.ko.md" "$config_dir/porting-inventory.ko.md")
  node_e2e_inventory_files+=("$config_dir/porting-inventory.ko.md")
  node_e2e_runner_files+=("$config_dir/run_e2e.sh")
  required_node_e2e_files+=("$config_dir/feature-map.ko.md" "$config_dir/porting-inventory.ko.md" "$config_dir/run_e2e.sh")
done
[[ "${#required_node_e2e_files[@]}" == 24 ]] || {
  printf 'required_node_e2e_files must contain 24 files, count=%s\n' "${#required_node_e2e_files[@]}" >&2
  exit 1
}
for file in "${required_node_e2e_files[@]}"; do
  test -f "$file" || {
    printf 'missing required Node e2e file: %s\n' "$file" >&2
    exit 1
  }
done

banned_pattern='language'"-exchange"'|문서'"작성"
if rg -n "$banned_pattern" \
  "$plan_file" \
  "${node_e2e_doc_files[@]}"; then
  exit 1
fi

stale_pattern='in'"-progress"'|TB'"D"'|not'"-started"'|미구'"현"'|시작'"점"'|구현[[:space:]]전'
if rg -n "$stale_pattern" \
  "$plan_file" \
  "${node_e2e_doc_files[@]}"; then
  exit 1
fi

wildcard_gap_pattern='PS-'"[*]"'|SM-'"[*]"
if rg -n "$wildcard_gap_pattern" \
  "$plan_file" \
  "${node_e2e_doc_files[@]}"; then
  exit 1
fi

pending_status_pattern='[|][[:space:]]*pending[[:space:]]*[|]'
if rg -n "$pending_status_pattern" \
  "${node_e2e_inventory_files[@]}"; then
  exit 1
fi

gap_pattern='[|][^|]*[|][[:space:]]*(gap|partial|부분 구현)[[:space:]]*[|]|gap scenario:|supplemental [^:]* gap:'
rg -n "$gap_pattern" "${node_e2e_doc_files[@]}"

required_gap_doc_refs=(
  # Mirrors the target IDs in "후속 gap 정리"; update both places together.
  # No current Node E2E gap refs.
)
[[ "${#required_gap_doc_refs[@]}" == 0 ]] || {
  printf 'required_gap_doc_refs must contain 0 entries, count=%s\n' "${#required_gap_doc_refs[@]}" >&2
  exit 1
}
plan_gap_section="$(
  awk '
    /^## 후속 gap 정리$/ { flag=1 }
    /^## 현재 checkout 상태 검증 게이트$/ { flag=0 }
    flag { print }
  ' "$plan_file"
)"
plan_order_section="$(
  awk '
    /^## 진행 순서$/ { flag=1 }
    /^## 현재 진행 상태$/ { flag=0 }
    flag { print }
  ' "$plan_file"
)"
current_status_section="$(
  awk '
    /^## 현재 진행 상태$/ { flag=1 }
    /^## 후속 gap 정리$/ { flag=0 }
    flag { print }
  ' "$plan_file"
)"
followup_input_section="$(
  awk '
    /^### 첫 작업 묶음 시작 입력$/ { flag=1 }
    /^후속 작업은 아래 산출물을 함께 갱신해야 한다[.] 계약 문서만 바꾸거나 e2e만 통과시키는 상태는 완료로 보지$/ { flag=0 }
    flag { print }
  ' "$plan_file"
)"
expected_config_order="$(printf '%s\n' "${node_e2e_configs[@]}")"
plan_order_configs="$(
  awk -F'|' '
    $2 ~ /^[[:space:]]*[0-9]+[[:space:]]*$/ &&
    $3 ~ /`(RegistryMessaging|PubSub|RegistrationCodec|DiscoveryRegistryHa|ResilienceLifecycle|RuntimeMonitoring|SpotService|YieldDispatch)`/ {
      gsub(/^[[:space:]]*`?|`?[[:space:]]*$/, "", $3)
      print $3
    }
  ' <<<"$plan_order_section"
)"
current_status_configs="$(
  awk -F'|' '
    $2 ~ /`(RegistryMessaging|PubSub|RegistrationCodec|DiscoveryRegistryHa|ResilienceLifecycle|RuntimeMonitoring|SpotService|YieldDispatch)`/ {
      gsub(/^[[:space:]]*`?|`?[[:space:]]*$/, "", $2)
      print $2
    }
  ' <<<"$current_status_section"
)"
[[ "$plan_order_configs" == "$expected_config_order" ]] || {
  printf '진행 순서 config order mismatch\nexpected:\n%s\nactual:\n%s\n' "$expected_config_order" "$plan_order_configs" >&2
  exit 1
}
[[ "$current_status_configs" == "$expected_config_order" ]] || {
  printf '현재 진행 상태 config order mismatch\nexpected:\n%s\nactual:\n%s\n' "$expected_config_order" "$current_status_configs" >&2
  exit 1
}
for config in "${node_e2e_configs[@]}"; do
  grep -F "\`$config\`" <<<"$plan_order_section" >/dev/null || {
    printf 'missing config in 진행 순서: %s\n' "$config" >&2
    exit 1
  }
  grep -F "\`$config\`" <<<"$current_status_section" >/dev/null || {
    printf 'missing config in 현재 진행 상태: %s\n' "$config" >&2
    exit 1
  }
done
required_doc_log_refs=(
  'framework/languages/node/e2e/RegistryMessaging/feature-map.ko.md|logs/20260630-080014-3192638'
  'framework/languages/node/e2e/PubSub/feature-map.ko.md|logs/20260630-093248-3426974'
  'framework/languages/node/e2e/RegistrationCodec/feature-map.ko.md|logs/20260630-102915-3514768'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022102-2343678'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022108-2344016'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022113-2344464'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022120-2344980'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022134-2345758'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-043800-2668198'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022140-2346239'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022148-2346991'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022151-2347457'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022157-2347999'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022206-2348746'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022211-2349015'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022216-2349345'
  'framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md|logs/20260630-022220-2349869'
  'framework/languages/node/e2e/ResilienceLifecycle/feature-map.ko.md|logs/20260630-061338-2889689'
  'framework/languages/node/e2e/RuntimeMonitoring/feature-map.ko.md|logs/20260630-064526-2998934'
  'framework/languages/node/e2e/SpotService/feature-map.ko.md|logs/20260630-101424-3467655'
  'framework/languages/node/e2e/YieldDispatch/feature-map.ko.md|logs/20260630-102243-3490701'
)
[[ "${#required_doc_log_refs[@]}" == 21 ]] || {
  printf 'required_doc_log_refs must contain 21 entries, count=%s\n' "${#required_doc_log_refs[@]}" >&2
  exit 1
}
required_doc_log_ids="$(printf '%s\n' "${required_doc_log_refs[@]}" | sed 's/.*|//' | sort -u)"
[[ "$(printf '%s\n' "$required_doc_log_ids" | wc -l)" == 21 ]] || {
  printf 'required_doc_log_refs must contain 21 unique log refs\n' >&2
  exit 1
}
for entry in "${required_doc_log_refs[@]}"; do
  file="${entry%%|*}"
  log_ref="${entry#*|}"
  rg --no-ignore -F "$log_ref" "$file" >/dev/null || {
    printf 'missing proof log reference: %s in %s\n' "$log_ref" "$file" >&2
    exit 1
  }
  grep -F "$log_ref" <<<"$current_status_section" >/dev/null || {
    printf 'missing proof log reference: %s in 현재 진행 상태\n' "$log_ref" >&2
    exit 1
  }
done
if ((${#required_gap_doc_refs[@]} == 0)); then
  expected_gap_ids=""
else
  expected_gap_ids="$(printf '%s\n' "${required_gap_doc_refs[@]}" | sed 's/.*|//' | sort -u)"
fi
expected_gap_count="$(printf '%s\n' "$expected_gap_ids" | sed '/^$/d' | wc -l)"
[[ "$expected_gap_count" == 0 ]] || {
  printf 'required_gap_doc_refs must contain 0 unique gap ids, count=%s\n' "$expected_gap_count" >&2
  exit 1
}
actual_gap_ids="$(
  {
    rg --no-ignore --no-filename '^[|][[:space:]]*`?([A-Z]+-[A-Z0-9]+|sm-q[0-9]+)`?[[:space:]]*[|].*[|][[:space:]]*(gap|partial|부분 구현)[[:space:]]*[|]' \
      "${node_e2e_doc_files[@]}"
    rg --no-ignore --no-filename 'gap scenario:|supplemental [^:]* gap:' \
      "${node_e2e_doc_files[@]}"
  } | rg -o '`?([A-Z]+-[A-Z0-9]+|sm-q[0-9]+)`?' | tr -d '`' | sort -u
)"
unexpected_gap_ids="$(comm -23 <(printf '%s\n' "$actual_gap_ids" | sed '/^$/d') <(printf '%s\n' "$expected_gap_ids" | sed '/^$/d'))"
if [[ -n "$unexpected_gap_ids" ]]; then
  printf 'gap ids exist in config docs but not required_gap_doc_refs:\n%s\n' "$unexpected_gap_ids" >&2
  exit 1
fi
for entry in "${required_gap_doc_refs[@]}"; do
  config="${entry%%|*}"
  scenario="${entry#*|}"
  grep -F "$scenario" <<<"$current_status_section" >/dev/null || {
    printf 'missing current status gap id: %s in 현재 진행 상태\n' "$scenario" >&2
    exit 1
  }
  grep -F "$scenario" <<<"$plan_gap_section" >/dev/null || {
    printf 'missing plan gap id: %s in 후속 gap 정리\n' "$scenario" >&2
    exit 1
  }
  grep -F "$scenario" <<<"$followup_input_section" >/dev/null || {
    printf 'missing follow-up input gap id: %s in 후속 작업 시작 입력\n' "$scenario" >&2
    exit 1
  }
  feature_file="framework/languages/node/e2e/$config/feature-map.ko.md"
  inventory_file="framework/languages/node/e2e/$config/porting-inventory.ko.md"
  feature_pattern='^[|][[:space:]]*`?'"$scenario"'`?[[:space:]]*[|][[:space:]]*(gap|partial|부분 구현)[[:space:]]*[|]'
  rg --no-ignore "$feature_pattern" "$feature_file" >/dev/null || {
    printf 'missing gap status: %s in %s\n' "$scenario" "$feature_file" >&2
    exit 1
  }
  inventory_pattern='^[|][[:space:]]*`?'"$scenario"'`?[[:space:]]*[|][^|]*[|][^|]*[|][[:space:]]*(gap|partial|부분 구현)[[:space:]]*[|]'
  rg --no-ignore "$inventory_pattern" "$inventory_file" >/dev/null || {
    printf 'missing gap status: %s in %s\n' "$scenario" "$inventory_file" >&2
    exit 1
  }
done

for f in "${node_e2e_runner_files[@]}"; do
  bash -n "$f" || exit 1
done

proof_logs=(
  framework/languages/node/e2e/RegistryMessaging/logs/20260630-080014-3192638
  framework/languages/node/e2e/PubSub/logs/20260630-093248-3426974
  framework/languages/node/e2e/RegistrationCodec/logs/20260630-102915-3514768
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022102-2343678
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022108-2344016
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022113-2344464
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022120-2344980
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022134-2345758
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-043800-2668198
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022140-2346239
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022148-2346991
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022151-2347457
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022157-2347999
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022206-2348746
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022211-2349015
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022216-2349345
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022220-2349869
  framework/languages/node/e2e/ResilienceLifecycle/logs/20260630-061338-2889689
  framework/languages/node/e2e/RuntimeMonitoring/logs/20260630-064526-2998934
  framework/languages/node/e2e/SpotService/logs/20260630-101424-3467655
)
[[ "${#proof_logs[@]}" == 20 ]] || {
  printf 'proof_logs must contain 20 log dirs, count=%s\n' "${#proof_logs[@]}" >&2
  exit 1
}
[[ "$(printf '%s\n' "${proof_logs[@]}" | sort -u | wc -l)" == 20 ]] || {
  printf 'proof_logs must contain 20 unique log dirs\n' >&2
  exit 1
}
for log_dir in "${proof_logs[@]}"; do
  test -f "$log_dir/client.stdout.log" || exit 1
  rg --no-ignore -n 'e2e result=passed|scenario .* passed' "$log_dir/client.stdout.log" || exit 1
done

check_markers() {
  local file="$1"
  shift
  for marker in "$@"; do
    rg --no-ignore -F "$marker" "$file" >/dev/null || {
      printf 'missing marker: %s in %s\n' "$marker" "$file" >&2
      exit 1
    }
  done
}

check_markers \
  framework/languages/node/e2e/RegistryMessaging/logs/20260630-080014-3192638/client.stdout.log \
  'scenario RM-A1 passed' 'scenario RM-A2 passed' 'scenario RM-A4 passed' 'scenario RM-A6 passed' \
  'scenario RM-B1 passed' 'scenario RM-B2 passed' 'scenario RM-C1 passed' 'scenario RM-C2 passed' \
  'scenario RM-C3 passed' 'scenario RM-C4 passed' 'scenario RM-C5 passed' 'scenario RM-C7 passed' 'scenario RM-C8 passed' \
  'scenario RM-C9 passed' \
  'registry-messaging e2e result=passed'

check_markers \
  framework/languages/node/e2e/PubSub/logs/20260630-093248-3426974/client.stdout.log \
  'scenario PS-A1 passed' 'scenario PS-A2 passed' 'scenario PS-A3 passed' 'scenario PS-A4 passed' \
  'scenario PS-B1 passed' 'scenario PS-B2 passed' 'scenario PS-C1 passed' 'pubsub e2e result=passed'

check_markers \
  framework/languages/node/e2e/RegistrationCodec/logs/20260630-102915-3514768/client.stdout.log \
  'scenario RC-A1 passed' 'scenario RC-A2 passed' 'scenario RC-A3 passed' 'scenario RC-A4 passed' 'scenario RC-A5 passed' \
  'scenario RC-A6 passed' 'scenario RC-B1 passed' 'scenario RC-B2 passed' 'scenario RC-B3 passed' \
  'scenario RC-B4 passed' 'scenario RC-B5 passed' 'registration-codec e2e result=passed'

check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022102-2343678/client.stdout.log \
  'scenario DR-A1 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022108-2344016/client.stdout.log \
  'scenario DR-A2 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022113-2344464/client.stdout.log \
  'scenario DR-A3 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022120-2344980/client.stdout.log \
  'scenario DR-A4 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022134-2345758/client.stdout.log \
  'scenario DR-B1 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-043800-2668198/client.stdout.log \
  'scenario DR-B2 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022140-2346239/client.stdout.log \
  'scenario DR-B3 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022148-2346991/client.stdout.log \
  'scenario DR-C1 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022151-2347457/client.stdout.log \
  'scenario DR-C2 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022157-2347999/client.stdout.log \
  'scenario DR-C3 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022206-2348746/client.stdout.log \
  'scenario DR-D1 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022211-2349015/client.stdout.log \
  'scenario DR-D2 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022216-2349345/client.stdout.log \
  'scenario DR-D3 passed' 'discovery-registry-ha e2e result=passed'
check_markers \
  framework/languages/node/e2e/DiscoveryRegistryHa/logs/20260630-022220-2349869/client.stdout.log \
  'scenario DR-D4 passed' 'discovery-registry-ha e2e result=passed'

check_markers \
  framework/languages/node/e2e/ResilienceLifecycle/logs/20260630-061338-2889689/client.stdout.log \
  'scenario RL-A1 passed' 'scenario RL-A2 passed' 'scenario RL-A3 passed' 'scenario RL-A4 passed' \
  'scenario RL-A5 passed' \
  'scenario RL-B1 passed' 'scenario RL-B2 passed' 'scenario RL-B3 passed' 'scenario RL-B4 passed' \
  'scenario RL-B6 passed' \
  'scenario RL-C1 passed' 'scenario RL-C2 passed' 'scenario RL-C3 passed' 'scenario RL-C4 passed' \
  'scenario RL-D1 passed' 'scenario RL-D2 passed' 'scenario RL-D3 passed' 'scenario RL-D4 passed' \
  'scenario RL-D5 passed' 'resilience-lifecycle e2e result=passed'

check_markers \
  framework/languages/node/e2e/RuntimeMonitoring/logs/20260630-064526-2998934/client.stdout.log \
  'scenario MON-A1 passed' 'scenario MON-A2 passed' 'scenario MON-A3 passed' 'scenario MON-A4 passed' 'scenario MON-A5 passed' 'scenario MON-B1 passed' \
  'scenario MON-B2 passed' 'scenario MON-C1 passed' 'scenario MON-D1 passed' \
  'runtime-monitoring e2e result=passed'

check_markers \
  framework/languages/node/e2e/SpotService/logs/20260630-101424-3467655/client.stdout.log \
  'scenario SM-A1 passed' 'scenario SM-A6 passed' 'scenario SM-A7 passed' 'scenario SM-A8 passed' \
  'scenario SM-B1 passed' 'scenario SM-B2 passed' 'scenario SM-B3 passed' 'scenario SM-B4 passed' \
  'scenario SM-B5 passed' 'scenario SM-B6 passed' 'scenario SM-B7 passed' 'scenario SM-B8 passed' \
  'scenario SM-C2 passed' 'scenario SM-C4 passed' 'scenario SM-D1 passed' \
  'scenario SM-D2 passed' 'scenario SM-D3 passed' 'scenario SM-D4 passed' 'scenario SM-D5 passed' 'scenario SM-D6 passed' \
  'scenario SM-D7 passed' 'scenario SM-D8 passed' 'scenario SM-D9 passed' 'scenario SM-D10 passed' \
  'scenario SM-D11 passed' 'scenario SM-D12 passed' 'scenario SM-D13 passed' 'scenario SM-D14 passed' 'scenario SM-E2 passed' \
  'scenario SM-E4 passed' 'scenario SM-F3 passed' 'scenario SM-F5 passed' 'spot-service e2e result=passed'

yield_log=framework/languages/node/e2e/YieldDispatch/logs/20260630-102243-3490701
for f in \
  "$yield_log/client.stdout.log" \
  "$yield_log/client-shutdown-recovery.stdout.log" \
  "$yield_log/client-shutdown-wait.stdout.log" \
  "$yield_log/static-checks.stdout.log"; do
  test -f "$f" || exit 1
done
rg --no-ignore -n 'scenario YD-E3 passed|yield-dispatch shutdown recovery result=passed' \
  "$yield_log/client-shutdown-recovery.stdout.log" || exit 1
rg --no-ignore -n 'yield-dispatch shutdown wait result=passed' \
  "$yield_log/client-shutdown-wait.stdout.log" || exit 1
rg --no-ignore -n 'scenario YD-E4 passed' \
  "$yield_log/static-checks.stdout.log" || exit 1
check_markers \
  "$yield_log/client.stdout.log" \
  'scenario YD-A1 passed' 'scenario YD-A2 passed' 'scenario YD-A3 passed' 'scenario YD-A4 passed' \
  'scenario YD-B1 passed' 'scenario YD-B2 passed' 'scenario YD-B3 passed' 'scenario YD-C1 passed' \
  'scenario YD-C2 passed' 'scenario YD-C3 passed' 'scenario YD-D2 passed' 'scenario YD-D3 passed' \
  'scenario YD-D4 passed' 'scenario YD-E1 passed' 'scenario YD-E2 passed' \
  'yield-dispatch e2e result=passed'

pgrep -af 'node .*e2[e]|node .*S[e]rver/|node .*C[l]ient/dist' || true
```

`node_e2e_configs`는 이 게이트가 다루는 8개 Node config의 단일 목록이다. 배열 길이 검사는 config가
실수로 빠지거나 늘어난 상태에서 게이트가 조용히 통과하지 않게 막는다. `required_plan_sections`는 이
게이트가 읽는 계획 문서 섹션이 정확히 한 번씩 남아 있는지 먼저 확인한다. 이 목록에서 문서 파일,
inventory 파일, runner 파일 배열을 만든 뒤 `required_node_e2e_files`로 실제 파일 존재를 확인한다.
이 파일 목록도 8개 config마다 3개 파일이라는 기준에 맞게 24개인지 검사한다. 같은 config 이름과 순서가
`진행 순서`와 `현재 진행 상태` 섹션에도 있는지 확인한다. 첫 두 `rg` 검사는 hit가 있으면
즉시 실패해야 한다. `wildcard_gap_pattern` 검사는 계획 문서와 config 문서에서 PubSub나 SpotService gap을
별표로 뭉뚱그려 쓰는 표기를 막는다. `pending_status_pattern` 검사는
`porting-inventory.ko.md` 표의 상태 셀에 남은 `pending`을 잡기 위한 별도 검사다. 일반 설명 안의
pending request 문구는 이 검사 대상이 아니다. `gap_pattern` 검색은 상태 셀이 `gap`/`partial`/`부분 구현`인
행과 `gap scenario:`/보충 gap 줄을 보여 주는 audit 입력이다. 완료 판정 전에는 이 출력의 각 행이
`후속 gap 정리`의 계약 결정, 구현 proof, 또는 제외 사유와 1:1로 맞아야 한다. 맞지 않는 행이 하나라도
있으면 marker가 통과해도 완료가 아니다.
`required_doc_log_refs`는 현재 진행 상태 표와 `DiscoveryRegistryHa` 상세 목록의 proof 로그가 이 계획 문서와
각 `feature-map.ko.md`에 함께 남아 있는지 확인한다. 이 배열은 현재 21개 entry와 21개 고유 로그 ref를
가져야 한다. `proof_logs`는 YieldDispatch의 별도 stdout 파일을 제외한 client stdout 직접 marker 검사 대상이라
20개 고유 로그 디렉터리를 가져야 한다. marker만 통과하고 계획 문서나 feature map이 오래된
로그를 가리키면 완료 proof로 쓰지 않는다. `required_gap_doc_refs`는 현재 `후속 gap 정리`의 대상 ID를 실행 가능한 검사 목록으로
옮긴 것이다. 이 목록은 현재 0개 entry와 0개 고유 gap ID를 가져야 한다. 개수가 달라지면 새 gap을
추가했거나 기존 gap을 닫은 것이므로 `현재 진행 상태`, `후속 gap 정리`, 후속 작업 시작 입력 절을 함께
갱신해야 한다. `actual_gap_ids`와 `expected_gap_ids` 비교는 config 문서에 새 gap/partial ID가 생겼는데 이
목록을 갱신하지 않은 경우를 잡는다. 현재처럼 기대 gap ID가 없으면 config 문서에 남은 gap/partial ID도
없어야 한다. 기대 gap ID가 생긴 경우에는 게이트가 각 ID가 이 계획 문서의 현재 상태 절과 후속 gap 절에
남아 있는지 확인한 뒤, `followup_input_section`에서도 같은 ID를 찾는다. 이 검사는 특정 ID가 후속 작업
묶음 표에서 빠진 채 전체 gap 목록에만 남는 경우를 잡는다. 그다음 같은 ID가 `feature-map.ko.md`와
`porting-inventory.ko.md` 양쪽에서 아직 gap/partial 상태인지 확인한다. 한쪽 문서만 완료 상태로 바꾸거나
ID만 설명에 남긴 경우에는 완료 proof로 보지 않는다. `bash -n`은 모든 runner에서 성공해야 한다. `proof_logs`에는
`현재 진행 상태` 표와 `DiscoveryRegistryHa` 상세 목록에 적은 최신 proof만 넣는다. 오래된 선택 PASS나
실패 로그는 gap 원인 증거로 남아 있을 수 있으므로 완료 proof 검색 대상에 섞지 않는다. 게이트 스크립트
자체를 수정한 뒤에는 `현재 checkout 상태 검증 게이트` 절의 `bash` 코드 펜스를 임시 파일로 추출해 `bash -n`으로
문법을 검사한다. `YieldDispatch`는 shutdown recovery와 static check marker가 별도 stdout 파일에 남으므로 그
파일들을 따로 확인한다. 잔류 프로세스 검색은 실제 Node E2E 프로세스가 남아 있지 않은지 확인하기 위한
보조 증거다.

게이트 스크립트 문법 검사는 아래처럼 수행한다.

```bash
awk '/^## 현재 checkout 상태 검증 게이트$/{section=1} section && /^```bash$/{flag=1; next} flag && /^```$/{exit} flag {print}' \
  framework/doc/plan/framework-node-e2e-dotnet-porting-plan.ko.md > /tmp/framework-node-plan-gate.sh
bash -n /tmp/framework-node-plan-gate.sh
```

문법 검사는 전체 게이트 실행을 대체하지 않는다. 문법 검사가 통과하면 `/tmp/framework-node-plan-gate.sh`를
실행해 marker와 문서 동기화 검사가 실제로 통과하는지 확인한다.

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
완료 판정은 전체 `SpotService/run_e2e.sh`, 남은 public contract gap 정리, Codex 에이전트 리뷰가 모두
끝난 뒤에만 한다. 구현 범위만 통과한 상태는 완료가 아니라 follow-up이 남은 상태로 기록한다.

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

`YieldDispatch`는 Node public surface 범위 안에서 YD-A1~YD-E5를 구현했다. public contract 근거가 없는
기능은 추가하지 않았고, cross-language aggregation은 Node config 완료 조건이 아니라 별도 parity gate 입력으로
분리했다.

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

- 같은 커밋에서는 한 config와 그 config에 필요한 공통 Node build 설정만 묶는다.
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
