# .NET common sample spec gap audit와 수정 ledger

> 상태: 선행 `.NET Framework` gap ledger가 완료된 뒤 착수할 다음 작업의 계획 문서. 현재 sample 구현과
> 정식 sample 문서는 수정하지 않았다.
>
> 기준: 2026-08-02 현재 working tree. 공통 sample 문서와 `.NET` sample source·test에는 기존 변경이
> 있으므로, 이 문서는 그 변경을 덮어쓰지 않고 현재 상태를 근거로 후속 작업 순서만 고정한다.
>
> 선행 조건: [`dotnet-framework-spec-gap-ledger.ko.md`](../dotnet-framework-spec-gap-ledger/dotnet-framework-spec-gap-ledger.ko.md)의
> implementation, E2E, contract regression과 package 검증 gate가 완료 상태여야 한다.

## 1. 목적과 완료 조건

이 문서는
[`framework/doc/framework/common/sample/`](../../framework/common/sample/README.ko.md)의 공통 sample
계약과 `framework/languages/dotnet/samples/`의 `.NET` 구현을 비교하여 gap을 확인하고, 확인된 차이를
수정하는 순서를 정한다. 비교 대상은 sample 이름이나 public API의 존재 여부에 한정하지 않는다. 다음
항목을 같은 기준으로 대조한다.

- message 이름, field 이름과 타입, nullable·optional 의미, enum 또는 named string 값
- request/reply, one-way send, notify, publish의 transport 의미와 handler 등록 방식
- payload가 wire에서 표현되는 방식과 typed JSON 또는 Protobuf codec의 선택
- topology, route, actor·spot ownership, session binding과 relocation의 실행 순서
- state commit, idempotency, retry·deadline, failure와 cleanup의 책임 경계
- Domain/Application/Infrastructure 분리와 application message에 내부 식별자를 노출하지 않는 규칙
- runner의 build, Redis 격리, readiness, client self-check, evidence와 cleanup 순서
- shell·PowerShell runner, 실제 process E2E, browser client와 회귀 test의 범위

다음 조건을 모두 만족해야 이 ledger의 sample 작업이 완료된다.

1. 공통 sample 7종의 정식 문서와 `.NET` shared contract를 한 행씩 대조한 inventory가 있고, 각 행의
   판정이 `충족`, `수정 완료`, `contract 선행` 중 하나로 닫힌다.
2. 공통 문서에 없는 `.NET` public 또는 client-facing message를 다른 언어 구현만으로 정당화하지
   않는다. 필요한 계약 변경은 먼저 공통 sample 문서와 관련 spec/guide의 review 대상으로 분리한다.
3. 확인된 wire shape와 runtime path gap을 소유한 계층에서 수정한다. sample 호출부에 raw frame, private
   API, reflection, message별 codec registry 또는 임시 adapter를 추가하지 않는다.
4. 모든 공통 sample의 직접적인 client assertion과 server evidence가 실제 process 실행에서 남는다.
   로그 문자열만으로 성공을 판정하지 않는다.
5. 지원하는 실행 환경의 shell·PowerShell runner가 같은 sample inventory와 완료 조건을 사용한다.
6. sample regression, 관련 framework regression, package를 사용하는 process E2E가 모두 통과한다.
7. 마지막 독립 재검토에서 기록되지 않은 `.NET` sample spec·구현 gap이 0개다.

이 문서의 작성 단계에서는 구현과 test source를 바꾸지 않는다. 구현 phase는 이 ledger의 선행 조건과
`contract 선행` 항목을 review로 닫은 뒤 시작한다.

## 2. 기준 문서와 조사 범위

공통 sample 문서는 실행 가능한 workflow, topology, message 계약, self-check와 완료 evidence를
소유한다. Framework public API의 계약 자체는 공통 Framework spec과 언어별 exact interface가 소유한다.
공통 sample 문서나 다른 언어 구현만으로 새 public API를 추가하지 않는다.

| 구분 | 기준 위치 | 이번 비교에서 확인할 내용 |
|---|---|---|
| 공통 sample index | `framework/doc/framework/common/sample/README.ko.md` | sample 목록, 공통 금지사항, 실행·완료 규칙 |
| 공통 sample 계약 | `bingo/README.ko.md`, `tictactoe/README.ko.md`, `supportchat/README.ko.md`, `deliverydispatch/README.ko.md`, `event/shoppingmall.ko.md`, `event/gamequest.ko.md`, `zoneworld/README.ko.md` | topology, message schema, flow, self-check, evidence |
| 공통 fixture | `framework/doc/framework/common/sample/fixtures/channel-topology.json` | 채널·mesh 이름과 역할의 공통 source |
| 공통 runner template | `framework/doc/framework/common/sample/runner-templates/` | readiness, Redis 격리, cleanup, 종료 marker |
| `.NET` sample source | `framework/languages/dotnet/samples/{Bingo,TicTacToe,SupportChat,DeliveryDispatch,ShoppingMall,GameQuest,ZoneWorld}/` | shared contract, server path, client assertion, runner |
| `.NET` sample runner | `framework/languages/dotnet/samples/run_samples.sh`, `run_samples.ps1`, 각 sample의 `run_sample.*` | 통합 inventory와 OS별 실행 순서 |
| `.NET` sample regression | `framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/` | 정적 구조 검증, runner 정책, sample별 regression |
| 공통 documentation regression | `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Documentation/Regression.cs` | 공통 문서와 `.NET` guide의 최소 동기화 조건 |
| 선행 framework ledger | `framework/doc/plan/dotnet-framework-spec-gap-ledger/dotnet-framework-spec-gap-ledger.ko.md` | Framework runtime·contract·E2E가 sample 작업의 선행 조건을 충족했는지 |
| package 정책 | `scripts/local-package/README.ko.md`, `framework/languages/dotnet/Directory.Packages.props` | sample이 실제로 사용하는 bindings/Core package와 version |

대상 sample은 다음 일곱 가지다.

| Sample | 공통 핵심 흐름 | `.NET` 실행 경로에서 확인할 결과 |
|---|---|---|
| Bingo | authentication, actor binding, match·room join, card·draw, reward와 relocation | Protobuf payload, room ownership, session·actor binding, room cleanup |
| TicTacToe | HTTP game 생성, authenticate·join, turn·milestone, leave와 actor destroy | JSON request/reply, `LeaveGame` one-way semantics, Entry Spot cleanup |
| SupportChat | agent availability, conversation join, greeting·typing, idle close와 reconnect | metadata, one-way typing, close·reconnect state와 session route |
| DeliveryDispatch | delivery 생성, courier offer·decision, status push, timeout·reassign | status 순서, timestamp wire shape, retry·deadline과 client assertion |
| ShoppingMall | order start, inventory·payment workflow, event·projection, idempotency·failure | 접수 응답, durable event, workflow compensation과 재시도 |
| GameQuest | session join, gameplay action, event·projection, replay·reconcile | action response inventory, typed payload, domain event와 stream/Redis path |
| ZoneWorld | browser/stream session, zone 이동, bot·ops, border relocation과 message follow | actor·spot ownership, 이동 message boundary, Chromium/browser evidence |

## 3. 선행 조건과 현재 검증 상태

### 3.1 선행 framework ledger gate

sample 실행이 의존하는 Framework runtime 의미를 sample source에서 우회해서는 안 된다. 따라서 다음
조건을 충족하기 전에는 이 ledger의 구현 card를 시작하지 않는다.

| 선행 조건 | 완료 판정 |
|---|---|
| Framework implementation audit | 기존 ledger의 `DN-IMP-001`~`DN-IMP-018`이 source, targeted test와 reviewer evidence로 닫힘 |
| Framework process E2E | 기존 ledger의 `DN-E2E-IMP-001`~`DN-E2E-IMP-017`에 실제 process 결과와 failure semantics가 기록됨 |
| Framework regression | 기존 ledger의 contract·unit·package·E2E regression card, 특히 `DN-REG-035`~`DN-REG-040`이 통과함 |
| Core·bindings package | 수정한 하위 layer가 있으면 package version, native runtime hash, consumer test와 `.NET` package cache가 일치함 |
| 독립 review | 선행 ledger의 final auditor가 sample이 사용할 public contract와 runtime 경계를 `CLEAN`으로 판정함 |

선행 card가 `선행 조건 미충족`, `contract 선행`, `test gap` 또는 unresolved finding이면 sample
구현에서는 raw frame, reflection, private member, 내부 상태 복제 또는 retry로 이를 숨기지 않는다. 원인을
소유한 ledger로 돌려보내고 이 문서에는 의존성만 기록한다.

### 3.2 현재 sample regression 실행 결과

현재 실행한 명령은 다음과 같다.

```text
cd framework/languages/dotnet
dotnet test tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --no-restore
```

결과는 `133 passed, 1 failed, 134 total`이다. 실패는 다음 regression이다.

| Test | 실패 내용 | 이 ledger의 처리 |
|---|---|---|
| `Zlink.Framework.SampleRegressionTests.RegressionTests.DotNet_Docs_Keep_Actor_Destroy_Entry_Owned` | `Regression.cs:382`에서 공통 TicTacToe 문서의 `Entry Spot context의 \`destroyActor\`를 호출한다` 문구를 찾지 못함 | 현재 working tree의 공통 문서 diff를 먼저 보존하고 계약 문구의 정식 표현을 review한다. 문구만 맞추어 test를 속이지 않고, 공통 문서·`.NET` guide·실행 evidence의 의미를 함께 닫는다. |

이 실패는 현재 문서 상태가 sample 계약과 동기화되지 않았음을 보여 주지만, 아직 어떤 문서를 어느
방향으로 바꿀지의 승인 증거는 아니다. 따라서 이 문서 작성 단계에서는 실패한 문서나 test를 수정하지
않는다.

### 3.3 이미 확인된 inventory·version 차이

다음은 별도의 실행을 통해 확인한 차이다.

| ID | 현재 근거 | 초기 판정 |
|---|---|---|
| `DS-IMP-008` | `run_samples.sh`의 `SAMPLES`에는 `ZoneWorld`가 있지만 `run_samples.ps1`의 `$knownSamples`에는 없다. | 확인된 runner parity gap |
| `DS-IMP-007` | `framework/languages/dotnet/samples/README.md`는 public `10.0.0` contract를 설명하지만 `Directory.Packages.props`의 `ZLinkBindingsPackageVersion`은 `11.1.0`이다. | 확인된 documentation/package version gap |
| `DS-IMP-007` | `.NET` guide의 integrated runner 설명은 여섯 sample과 ZoneWorld 별도 실행을 말하지만 현재 shell runner는 일곱 sample을 포함한다. | 확인된 guide·runner inventory gap |
| `DS-IMP-009` | sample regression은 여러 구조 규칙을 검증하지만, 공통 sample의 모든 message·field·flow·evidence와 `.NET` client/server path를 한 번에 대조하는 inventory test가 없다. | 확인된 test/evidence gap |

## 4. gap 판정 규칙

각 finding은 다음 상태 가운데 하나로 관리한다.

| 상태 | 의미 | 다음 행동 |
|---|---|---|
| `확인` | 공통 문서와 `.NET` source 또는 process path의 차이를 재현 가능한 근거로 확인했다. | 원인 owner를 정하고 regression을 먼저 고정한 뒤 수정한다. |
| `contract 선행` | 공통 문서 자체에 응답 유무, field shape 또는 public 범위가 모호하거나 서로 충돌한다. | 구현을 바꾸지 말고 공통 문서·spec·guide review에서 계약을 먼저 확정한다. |
| `test gap` | 구현이 맞을 가능성이 있어도 현재 test가 해당 계약을 직접 판정하지 않는다. | exact inventory, wire assertion 또는 process evidence를 추가한다. |
| `documentation gap` | source와 실행 path는 같지만 문서·runner 목록·version 설명이 현재 상태와 다르다. | 정식 문서 owner에서 근거를 갱신하고 regression을 추가한다. |
| `충족` | source, wire 결과, 실행 evidence와 test가 같은 계약을 직접 증명한다. | 근거 경로와 명령을 기록하고 다시 열지 않는다. |

기능 이름이 같거나 build가 성공했다는 사실만으로 `충족`으로 분류하지 않는다. handler의 transport
종류, response의 field, state commit 시점, failure 후 cleanup까지 실제 path를 따라간다.

## 5. 구현 수준에서 확인된 gap

### DS-IMP-001 — one-way message의 이름과 의미가 공통 문서와 다름

**현재 판정: `확인`; 공통 문서 변경 diff를 먼저 동결한 뒤 최종 수정 방향을 정한다.**

공통 TicTacToe 문서는 `LeaveGameMsg`를 선언하고, actor가 보내는 one-way message이며 response를
기다리지 않는다고 설명한다. 현재 `.NET` shared contract, handler, client와 runner는
`LeaveGameReq`를 사용한다. 공통 SupportChat 문서는 같은 규칙으로 `SetTypingMsg`를 선언하지만 `.NET`
은 `SetTypingReq`를 사용한다.

근거:

- 공통 문서: `framework/doc/framework/common/sample/tictactoe/README.ko.md`의 message 선언과 leave flow,
  `framework/doc/framework/common/sample/supportchat/README.ko.md`의 typing contract
- `.NET`: `TicTacToe/Shared/Contracts/Messages.cs`, `TicTacToe/.../PlayActorLeaveGameHandler.cs`,
  `TicTacToe/Client/TicTacToeClientScenario.cs`, `SupportChat/Shared/Contracts/Messages.cs`,
  `SupportChat/.../SetTypingHandler.cs`

수정 전에 확인할 내용은 다음과 같다.

1. 공통 sample의 `Req/Res`, `Msg`, `Notify`, `Event` 명명 규칙이 public wire contract인지, 문서상의
   역할 설명인지 확인한다.
2. one-way send가 실제로 response subscription이나 implicit completion을 사용하지 않는지 확인한다.
3. 계약이 확정되면 모든 `.NET` shared contract, handler attribute, client scenario, runner evidence와
   회귀 test를 같은 이름·transport 의미로 맞춘다. 이름만 바꾸고 request/reply path를 남기지 않는다.

### DS-IMP-002 — GameQuest action inventory와 `GameplayMsg.payload`가 공통 계약과 다름

**현재 판정: `contract 선행`과 `확인`이 함께 있다. action 범위는 계약 review 후 수정하고, payload wire
shape는 실제 serializer 결과까지 확인한다.**

공통 GameQuest 문서는 `KillMonsterReq/Res`, `CollectItemReq`, `EnterAreaReq`,
`GetQuestProgressReq/Res`, `SyncQuestProgressReq/Res`를 선언하며 `GameplayMsg.payload`를 `object`로
설명한다. 현재 `.NET`은 `CollectItemRes`, `CompleteMissionReq/Res`, `EnterAreaRes`,
`UnlockFeatureReq/Res`를 추가하고, client가 이 응답을 요청한다. `GameplayMsg.Payload`는 `byte[]`이며
`GameplayEventOwnerDispatcher`와 `QuestContractMapper`가 UTF-8 byte payload를 직접 직렬화·역직렬화한다.
또한 공통 문서의 durable domain record 이름과 `.NET`의 `QuestProgressedEvent` 등 이름에 `Event` 접미어
차이가 있다.

공통 문서의 action 설명 일부는 각 action response가 EventId를 만든다고 서술하므로 declaration과
본문이 완전히 일치하지 않는다. 따라서 `.NET`의 추가 action을 바로 제거하거나 공통 문서에 바로 추가하지
않는다.

수정 순서:

1. action별 request/reply 유무, EventId 반환, public client surface 여부를 공통 sample contract에서
   하나의 표로 확정한다.
2. `GameplayMsg.payload`가 typed JSON object인지, envelope 안의 bytes인지, domain record의 저장
   payload와 transport payload를 어떻게 구분하는지 결정한다.
3. 계약 확정 뒤 `.NET` shared message, handler, client scenario와 mapper를 한 경계에서 수정한다.
   호출부에 `encode`, `decode`, `serialize`, `parse`를 추가하는 우회는 허용하지 않는다.
4. durable domain record 명칭은 transport message와 분리해 문서·source·store mapper의 용어를 같은
   의미로 맞춘다.

### DS-IMP-003 — ShoppingMall 접수 응답의 field shape와 workflow message 이름이 다름

**현재 판정: `확인`; 내부 command 이름은 public wire contract인지 먼저 분리하고, 접수 응답 field는
직접 대조한다.**

공통 ShoppingMall 문서는 `StartOrderRes { orderId: string; state: OrderState }`를 선언한다. 현재 `.NET`
`ShoppingMall/Shared/Contracts/Messages.cs`의 `StartOrderRes`는 `OrderId`와 `Status` 문자열을 가진다.
또한 공통 문서의 workflow message 이름은 `ReserveInventoryReq/Res`, `ReleaseInventoryReq/Res`,
`AuthorizePaymentReq/Res`인데, `.NET` 내부 workflow에는 `ReserveInventoryCommand/Result` 등의 이름이
사용된다.

확인할 항목:

- `StartOrderRes`가 실제 client wire payload에서 `state` enum 또는 named string을 보내는지
- `Status`가 public contract의 `OrderState`와 같은 값 집합·nullable 의미를 갖는지
- `Command/Result`가 Domain/Application 내부 타입인지, 다른 Spot/Actor에 전송되는 public message인지
- idempotency 재호출, workflow failure와 compensation에서 접수 응답과 event 순서가 공통 flow와 같은지

수정은 public wire shape를 먼저 맞추고, 내부 type 이름은 Domain/Application 경계를 넘는지 확인한 뒤
필요한 범위에서만 바꾼다. 내부 이름을 맞추기 위해 application message를 새로 노출하지 않는다.

### DS-IMP-004 — DeliveryDispatch timestamp의 wire type이 다름

**현재 판정: `확인`; JSON 또는 Protobuf의 실제 wire 값을 직접 캡처해 최종 판정한다.**

공통 DeliveryDispatch 문서는 `DeliveryStatusChangedReq`, `DeliveryStatusNotify`,
`DeliveryStatusUpdatedMsg`의 `occurredAtUnixMs: int64`를 요구한다. 현재 `.NET`
`DeliveryDispatch/Shared/Contracts/Messages.cs`는 해당 값에 `DateTimeOffset OccurredAt`을 사용한다.
server evidence에서 `ToUnixTimeMilliseconds()`를 호출하는 부분이 있어 저장·로그 표현과 transport
payload가 다를 가능성이 있다.

수정 순서:

1. client가 수신한 raw application payload를 업무 코드에서 해석하지 않는 별도 test harness로 확인한다.
2. 공통 계약의 `int64`와 `.NET` serializer가 실제로 같은 JSON number를 만드는지 확인한다.
3. 다르면 shared contract와 모든 handler·client assertion을 Unix milliseconds로 맞추고, 같다면
   `DateTimeOffset` 표현을 내부 type으로 명시하여 public wire contract와 혼동되지 않도록 문서화한다.
4. status ordering, late decision과 reassign test에 timestamp 비교를 포함한다.

### DS-IMP-005 — Bingo client-facing message와 optional field가 공통 목록과 다름

**현재 판정: `확인`; extra message의 public 범위는 contract review가 필요하다.**

공통 Bingo message 목록에는 `BingoJoinFailedNotify`와 `BingoActorEntrySpotNotify`가 명시되어 있지
않지만 `.NET` Protobuf schema에는 두 message가 있고 `PlayerActor`와 Entry Spot이 session으로
전송한다. 공통 `AuthenticatePlayerRes`는 `actor_id`, `display_name`, `reason`을 optional로
설명하지만 `.NET` proto는 plain `string` field로 선언한다.

확인할 항목:

- 두 notify가 client가 관찰해야 하는 public push인지, sample 내부 evidence인지
- optional field가 미설정·빈 문자열·null을 구분해야 하는지
- 모든 언어가 같은 field presence와 default 값을 제공해야 하는지
- notify를 공통 계약에 추가할지, client-facing surface가 아니면 `.NET` shared contract에서 분리할지

계약이 확정되기 전에는 다른 언어 구현을 근거로 새 public message를 추가하지 않는다. 확정 후 Protobuf
schema, client assertion, handler와 공통 문서의 message·flow·completion 조건을 함께 변경한다.

### DS-IMP-006 — ZoneWorld 이동이 공통 message 경계를 우회함

**현재 판정: `확인`; 기능 존재가 아니라 Actor → Zone Spot 전달 방식의 차이다.**

공통 ZoneWorld 문서는 `UpdatePositionMsg`를 선언하고, 같은 zone의 이동은 Actor가 이 message를 Zone
Spot에 보내 state를 변경한다고 설명한다. 현재 `.NET` `ZoneWorld/Shared/Contracts/ZoneWorldMessages.cs`
에는 `UpdatePositionMsg`가 없으며, `PlayerMoveHandlers.cs`가 `actor.MoveTo(...)` 뒤 `ZoneSpot.UpdatePosition(...)`
을 직접 호출한다. `ZoneSpot.UpdatePosition`은 internal method다.

이 차이는 단순한 이름 차이가 아니다. message route, queue·handler 순서, owner commit과 state
publication이 Framework runtime의 public path를 거치지 않게 된다.

수정 순서:

1. `UpdatePositionMsg`의 sender, target, one-way semantics와 same-zone·border relocation의 commit
   경계를 공통 문서에서 고정한다.
2. Framework public message handler를 통해 Zone Spot state를 갱신하도록 production path를 바꾼다.
3. application code가 internal `ZoneSpot.UpdatePosition`이나 actor reference를 직접 호출하지 않는지
   정적 test로 고정한다.
4. same-zone move, border crossing, actor relocation과 message follow를 실제 process E2E에서 확인한다.

### DS-IMP-007 — `.NET` sample 문서·guide·version 설명이 현재 실행 경로와 다름

**현재 판정: `documentation gap`; source 수정과 섞지 않고 문서 owner에서 별도로 닫는다.**

현재 `.NET` sample README는 public `10.0.0` framework contract를 설명하지만 package central version은
`11.1.0`이다. `.NET` server guide는 integrated runner가 여섯 sample을 처리하고 ZoneWorld를 별도로
실행한다고 설명하지만 shell integrated runner는 ZoneWorld를 포함한 일곱 sample을 선택한다.

수정 전에 다음을 확인한다.

- README가 historical contract 예제인지 현재 package contract 안내인지
- guide가 generated output인지 source owner가 어디인지
- package version을 문서에 고정할 필요가 있는지, 현재 version을 자동으로 읽어야 하는지
- ZoneWorld browser runner를 integrated sample 완료 조건에 포함할지

확정 후 README·guide·runner 목록과 version assertion을 한 변경으로 맞춘다. generated guide를 직접
수정하지 않고 생성 source를 고친다.

### DS-IMP-008 — PowerShell 통합 runner가 ZoneWorld를 제외함

**현재 판정: `확인`; 지원 OS 범위를 먼저 결정한다.**

`run_samples.sh`의 기본 sample 목록은 `TicTacToe Bingo SupportChat ShoppingMall DeliveryDispatch
GameQuest ZoneWorld`다. `run_samples.ps1`의 `$knownSamples`에는 `ZoneWorld`가 없다. 따라서 shell과
PowerShell의 기본 실행 범위가 다르고, 공통 sample 7종을 모두 실행했다는 판정을 Windows runner가
증명하지 못한다.

지원 범위가 동일하다는 결정을 내리면 다음을 수정한다.

1. PowerShell integrated runner에 ZoneWorld를 추가한다.
2. ZoneWorld의 PowerShell per-sample runner와 browser/static configuration 전달 경로가 없으면
   공통 runner 규칙에 맞춰 추가한다.
3. Windows에서 dedicated Redis, readiness, browser self-check, cleanup을 실제로 실행한다.

Windows에서 ZoneWorld를 지원하지 않기로 결정하면 공통 sample 문서와 `.NET` guide에 그 제한과 대체
검증 명령을 명시하고, runner regression이 제한을 누락으로 오인하지 않도록 계약을 갱신한다.

### DS-IMP-009 — 공통 sample 전체를 덮는 exact inventory와 process evidence가 없음

**현재 판정: `test gap`.**

현재 sample regression은 topology, codec 사용, runner 문자열, 일부 sample flow와 금지 API를 검사한다.
하지만 다음 관계를 한 번에 검사하는 기준이 없다.

- 공통 문서의 모든 message·field·transport kind와 `.NET` shared contract·handler의 대응
- 공통 flow의 각 단계와 `.NET` client scenario의 response·push·ordering assertion
- state commit, actor/spot ownership, relocation과 cleanup의 server evidence
- shell·PowerShell runner의 sample inventory와 실제 completion marker
- 공통 fixture의 topology와 `.NET` source가 사용하는 mesh·channel 값

이 gap은 source를 형식적으로 스캔하는 test 하나로 끝내지 않는다. static inventory와 최소 한 번의 실제
process smoke를 모두 추가한다.

## 6. Sample별 구현 path 검토 matrix

다음 matrix는 각 sample에서 이름 일치 외에 확인할 method-level 검토 범위를 고정한다. `검증 path`는
구현 phase에서 실제 line과 실행 log를 추가한다.

| Sample | 계약·동작 검토 범위 | `.NET`에서 먼저 읽을 path | 완료 evidence |
|---|---|---|---|
| Bingo | Protobuf field presence, authentication·actor binding, match reservation, room join·yield, reward publish, room relocation·cleanup | `Shared/Contracts/bingo_messages.proto`, `Server/Play`, `Server/Session`, `Client`, `run_sample.*` | 두 client의 card·draw·result assertion, room owner·actor relocation log, Redis cleanup |
| TicTacToe | HTTP create response, manual topology, join admission, turn order, milestone publish, `LeaveGameMsg` one-way과 Entry Spot destroy | `Shared/Contracts/Messages.cs`, `Server/Play/.../Handlers`, `Client/TicTacToeClientScenario.cs`, `run_sample.*` | response·GameState·milestone payload assertion, leave completion, destroy evidence |
| SupportChat | agent availability, conversation join, `SetTypingMsg` one-way, metadata propagation, idle close, reconnect와 session route | `Shared/Contracts/Messages.cs`, `Server/Support`, `Client/SupportChatClientScenario.cs`, `run_sample.*` | greeting·typing·close·reconnect ordering과 conversation owner evidence |
| DeliveryDispatch | offer/decision transport kind, status order, `occurredAtUnixMs` wire number, retry·deadline·reassign, late decision | `Shared/Contracts/Messages.cs`, `Server/Tracking`, `Server/CustomerGateway`, `Client`, `run_sample.*` | Assigned→Accepted→PickedUp→Delivered 또는 Reassigned sequence와 timestamp assertion |
| ShoppingMall | `StartOrderRes` state shape, internal workflow command 경계, durable event name, projection rebuild, idempotency, compensation | `Shared/Contracts/Messages.cs`, `Server/CommerceApi`, `Server/OrderWorkflow`, `Client`, `run_sample.*` | 접수 응답 field, event/projection 상태, 재호출·failure 결과와 store cleanup |
| GameQuest | action inventory, EventId response 의미, typed `GameplayMsg` payload, replay/reconcile, domain event와 store mapper 분리 | `Shared/Messages.cs`, `Server/GameApi`, `Server/QuestMission`, `Client/GameQuestClientScenario.cs`, `run_sample.*` | 각 action response, progress notify, replay/reconcile 결과와 no-transport-domain dependency |
| ZoneWorld | `UpdatePositionMsg` route, same-zone state update, border relocation·message follow, bot backpressure, ops replay, browser ws/wss/reconnect | `Shared/Contracts/ZoneWorldMessages.cs`, `Server/ZoneNode`, `Server/Gateway`, `Server/Ops`, browser client, `run_sample.sh` | browser self-check, same-zone/border state, owner·route evidence, process cleanup |

이 matrix에서 공통 문서와 `.NET` source의 용어가 다르면 먼저 `contract 선행`으로 이동한다. `.NET`
source를 기준으로 공통 문서를 축소하지 않는다.

## 7. 수정 순서와 card gate

### G0 — 선행 Framework ledger 완료 확인

기존 `.NET Framework` ledger의 implementation·E2E·regression·package gate를 다시 실행하고, sample이
사용할 Framework public path가 실제 package에 들어 있는지 확인한다. 이 단계에서는 sample source를
수정하지 않는다.

### G1 — 공통 sample 계약 동결과 모호성 분리

현재 working tree의 common sample 문서 diff를 별도 manifest로 보존한다. `DS-IMP-001`, `DS-IMP-002`,
`DS-IMP-005`, `DS-IMP-008`처럼 public message 범위나 지원 OS를 바꾸는 항목은 공통 문서·guide·다른
언어 구현을 함께 읽고 다음을 기록한다.

- 확정된 public message와 internal-only type
- transport kind와 response completion의 의미
- field type, optionality, enum/named string 값
- 지원 runner와 browser path
- 계약 변경이 필요한 경우 관련 spec/guide owner와 review 결과

계약이 확정되지 않은 항목은 구현 card로 이동하지 않는다.

### G2 — exact contract inventory와 실패 regression 고정

공통 sample 7종의 message·field·flow를 machine-readable 또는 test fixture로 만들고 `.NET` shared
contract와 비교한다. inventory의 한 행은 sample, message, direction, transport kind, response,
field shape, nullable, owner와 evidence를 포함한다. 이 단계의 test가 먼저 실패해야 G3 이후 source
수정의 범위를 알 수 있다.

### G3 — wire contract와 public sample path 정렬

`DS-IMP-001`~`DS-IMP-005`를 계약 결정에 따라 수정한다. shared message와 client/server handler를 함께
변경하고, serializer가 만드는 실제 payload를 assertion한다. 내부 workflow 이름이나 domain event
이름은 public wire contract와 분리한다. 새 public API, raw frame, reflection 또는 호출부 codec은
추가하지 않는다.

### G4 — runtime method path와 ownership 정렬

`DS-IMP-006`을 우선 수정하고 각 sample의 relocation, state commit, cleanup, retry·deadline 경계를
실제 call path로 다시 검사한다. ZoneWorld처럼 internal method 직접 호출이 공통 message boundary를
우회하는 경우 Framework public handler와 sample application 책임을 분리한다. G3의 wire 수정이
runtime semantics에 영향을 주면 같은 card에서 process E2E를 다시 실행한다.

### G5 — runner·guide·package 설명 정렬

`DS-IMP-007`과 `DS-IMP-008`을 지원 OS 결정 뒤 수정한다. shell·PowerShell 목록, per-sample runner,
ZoneWorld browser configuration, dedicated Redis와 cleanup을 같은 template 규칙으로 맞춘다. version
문서는 central package version의 owner를 기준으로 갱신하고 generated 문서는 생성 source를 수정한다.

### G6 — 회귀 test와 실제 process evidence 추가

G2에서 만든 inventory를 contract-like regression으로 고정하고, sample별 runner를 실제로 실행한다.
각 실행은 build → dedicated Redis/resource → server start → readiness → client self-check → evidence
수집 → cleanup 순서를 지켜야 한다. 한 sample의 로그가 남아 있다는 사실만으로 다른 sample의 완료를
추론하지 않는다.

### G7 — 최종 독립 audit

선행 Framework ledger와 이 ledger를 서로 다른 관점에서 다시 읽는다. source, common sample 문서,
`.NET` guide, runner, test, package version과 실제 evidence를 교차 대조하고, `확인`·`test gap`·
`contract 선행` 항목이 남아 있으면 완료로 표시하지 않는다.

각 card에는 기준 commit 또는 working tree manifest, 변경 파일, 실행 명령, 결과, reviewer finding과
재검토 결과를 기록한다. 구현 중 unrelated dirty change를 되돌리거나 덮어쓰지 않는다.

## 8. 기존 회귀 test의 유지와 변경 목록

### 8.1 계속 유지할 test

기존 test는 범위를 줄이지 않고 유지한다. 다음 test들이 이미 보장하는 topology·runner 정책·codec
금지·sample별 flow를 새 inventory test로 대체하지 않는다.

| Test 영역 | 현재 보장하는 내용 | sample ledger에서의 역할 |
|---|---|---|
| `Regression.cs` | public connector assertion surface, sample discovery, payload codec 정책, actor destroy 문서 조건 | 공통 문서의 최소 규칙을 유지하고 exact message inventory와 연결 |
| `BingoRegressionTests` | topology, relocation adapter, room join·dedupe, client card/draw와 Redis 격리 | Bingo 실행 방식의 기존 regression 유지 |
| `TicTacToeRegressionTests` | handler registration, manual topology, relocation, runner/evidence, lifecycle와 payload lifetime | `LeaveGame` semantics와 destroy evidence를 확장 |
| `SupportChatRegressionTests` | one mesh, handler scan, rejection, relocation과 Redis 격리 | typing·metadata·reconnect assertion을 확장 |
| `DeliveryDispatchRegressionTests` | topology, status order, location store, binder/response, readiness/no retry | timestamp wire type와 late decision을 확장 |
| `ShoppingMallRegressionTests` | owner topology, isolated stores, Domain boundary | `StartOrderRes`와 workflow event shape를 확장 |
| `GameQuestRegressionTests` | isolated Redis/stream, Domain dependency boundary | action·payload inventory와 replay/reconcile를 확장 |
| `ZoneWorldTopologyRegressionTests`, `ZoneWorldOpsConsoleRegistryTests` | physical mesh, global route, relocation gate, ops registry | `UpdatePositionMsg` boundary와 browser process evidence를 확장 |
| `SampleConfigurationPolicyRegressionTests` | config provider 금지, readiness, shell sample 목록, browser static config, backpressure | shell·PowerShell inventory parity와 실제 completion gate를 추가 |
| `ExecutionTurnRegressionTests` | scenario inventory, typed packet names, bounded evidence, canonical ID | 공통 flow 단계별 assertion과 연결 |

### 8.2 수정 또는 추가할 regression

아래 ID는 구현 phase에서 추가할 regression 목록이다. 현재 문서 작성 단계에서는 test source를 수정하지
않는다.

| ID | 대상 test | 추가·변경할 판정 |
|---|---|---|
| `DS-REG-001` | `CommonSampleContractInventoryMatchesDotNetSharedTypes` | 공통 7종의 message·field·direction·transport kind가 `.NET` shared contract와 일치하는지 확인 |
| `DS-REG-002` | `CommonSampleMessageSemanticsMatchDotNetHandlers` | `Msg`, `Req/Res`, `Notify`, `Event`가 실제 send/request/publish handler와 같은 의미인지 확인 |
| `DS-REG-003` | `CommonSampleOptionalAndEnumValuesMatchWireContract` | optional/null/default와 enum 또는 named string 값을 실제 serialized payload로 확인 |
| `DS-REG-004` | `TicTacToeLeaveUsesOneWayMessage` | `LeaveGameMsg`의 response subscription과 request/reply 우회를 금지하고 Entry Spot destroy evidence를 확인 |
| `DS-REG-005` | `SupportChatTypingUsesOneWayMessage` | typing send가 one-way이며 metadata·conversation route를 유지하는지 확인 |
| `DS-REG-006` | `GameQuestActionInventoryMatchesCommonContract` | extra action과 response를 계약 review 결과에 따라 허용하거나 실패시키고, 임의 public API를 금지 |
| `DS-REG-007` | `GameQuestGameplayPayloadMatchesTypedContract` | `GameplayMsg.payload`의 object/JSON wire shape와 store mapper의 domain conversion을 분리해 확인 |
| `DS-REG-008` | `ShoppingMallStartOrderResponseMatchesCommonShape` | `orderId`와 `state`의 타입·값 집합·idempotent 재호출 결과를 확인 |
| `DS-REG-009` | `DeliveryDispatchTimestampsUseCommonWireEncoding` | status request/notify/update의 `occurredAtUnixMs`가 같은 wire number와 ordering을 사용하는지 확인 |
| `DS-REG-010` | `BingoClientFacingMessagesMatchCommonInventory` | extra notify의 public 여부와 Protobuf optional presence를 공통 목록과 대조 |
| `DS-REG-011` | `ZoneWorldMoveUsesUpdatePositionMessageBoundary` | Actor 이동이 public message handler를 통해 Zone Spot state를 변경하고 internal method를 직접 호출하지 않는지 확인 |
| `DS-REG-012` | `IntegratedSampleRunnerIncludesEveryCommonSampleOnAllSupportedHosts` | shell·PowerShell sample 목록이 공통 7종과 같고, 지원하지 않는 host 제한은 문서화되었는지 확인 |
| `DS-REG-013` | `CommonSampleRunnerUsesIsolatedRedisAndCleanup` | sample별 dedicated Redis, readiness 실패 처리, process 종료와 cleanup을 양 OS에서 확인 |
| `DS-REG-014` | `CommonSampleCompletionRequiresClientAndServerEvidence` | payload·ordering self-check와 server ownership/cleanup evidence가 모두 있어야 성공으로 판정 |
| `DS-REG-015` | `CommonSampleTopologyUsesSharedFixture` | mesh·channel role이 공통 fixture와 일치하고 sample마다 임의 상수를 복사하지 않는지 확인 |
| `DS-REG-016` | `DotNetSampleDocsMatchRunnerAndPackageVersion` | sample README, generated guide, runner 목록과 central package version이 같은 기준을 쓰는지 확인 |
| `DS-REG-017` | `AllCommonSamplesRespectApplicationBoundaryRules` | Domain의 Framework/storage 의존, application message의 NodeRid·ActorRef·raw frame·reflection·message별 codec을 전 sample에서 금지 |
| `DS-REG-018` | `ZoneWorldBrowserAndProcessEvidenceIsComplete` | Chromium/browser ws/wss/reconnect, static config 전달, same-zone·border relocation 결과를 실제 실행으로 확인 |

현재 실패한 `DotNet_Docs_Keep_Actor_Destroy_Entry_Owned`는 `DS-REG-004`의 문서 표현과 함께 다시
검토한다. 공통 문서가 의도적으로 바뀐 경우에는 test가 낡은 문구를 강제하지 않도록 정식 표현을 먼저
확정하고, 구현 의미를 약화시키는 문자열 변경은 허용하지 않는다.

## 9. 실행·증거 수집 계획

구현 phase의 최소 검증 순서는 다음과 같다.

1. 공통 문서 inventory와 `.NET` source를 static 비교하고, 실패하는 `DS-REG`을 확인한다.
2. shared contract와 server/client project를 build한다. build 성공은 sample 완료 증거가 아니다.
3. `dotnet test tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj`
   와 영향받은 Framework contract/unit regression을 실행한다.
4. 지원 host마다 `run_samples.sh` 또는 `run_samples.ps1`로 공통 sample 목록을 실행한다. 각 sample은
   dedicated Redis와 고유 resource prefix를 사용한다.
5. ZoneWorld는 `.NET` process와 공통 TypeScript browser client를 함께 실행하고 Chromium 결과,
   static configuration, ws/wss/reconnect와 border relocation evidence를 보관한다.
6. client가 response·push payload와 ordering을 직접 assertion했는지 확인한다. runner log는 assertion과
   server evidence를 찾는 보조 자료로만 사용한다.
7. 종료 뒤 server, client, Redis와 temporary resource가 정리되었는지 확인한다. 이전 실행 log나 stale
   completion marker를 새 실행 결과로 사용하지 않는다.
8. 마지막으로 `git diff --check`, 변경 manifest, test 결과와 process evidence 경로를 card에 기록한다.

실행 결과에는 명령, host/runtime version, package version과 hash, 시작 시각, sample별 exit code,
assertion 수, evidence 파일, cleanup 결과를 포함한다. 실패한 sample을 제외한 나머지 성공만으로 전체
완료를 표시하지 않는다.

## 10. 완료 checklist

- [ ] 선행 `.NET Framework` ledger의 implementation·E2E·regression·package gate가 닫혔다.
- [ ] 현재 common sample 문서 diff와 기존 dirty source를 manifest로 보존했다.
- [ ] 공통 7종의 message·field·transport·flow inventory가 작성되고 `contract 선행` 항목이 분리됐다.
- [ ] `DS-IMP-001`~`DS-IMP-009`의 상태와 owner가 정해졌다.
- [ ] TicTacToe와 SupportChat one-way message semantics가 이름과 실행 path 모두 일치한다.
- [ ] GameQuest action·payload·domain event 계약이 공통 문서와 source에서 같은 의미를 갖는다.
- [ ] ShoppingMall 접수 응답과 DeliveryDispatch timestamp wire shape가 직접 검증된다.
- [ ] Bingo extra notify와 optional field의 public 범위가 review로 확정됐다.
- [ ] ZoneWorld가 공통 `UpdatePositionMsg` 경계, relocation/message follow와 browser flow를 충족한다.
- [ ] shell·PowerShell runner의 sample inventory와 ZoneWorld 지원 범위가 일치한다.
- [ ] `DS-REG-001`~`DS-REG-018` 중 적용 대상이 통과하고, 제외한 항목에는 근거가 있다.
- [ ] sample 7종의 실제 process 실행에서 client self-check, server evidence와 cleanup이 모두 확인됐다.
- [ ] `.NET` sample regression과 선행 Framework regression이 fresh package로 통과했다.
- [ ] 독립 final audit에서 기록되지 않은 sample spec·구현 gap이 0개다.

