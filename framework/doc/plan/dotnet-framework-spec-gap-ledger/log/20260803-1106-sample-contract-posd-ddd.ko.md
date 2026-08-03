# .NET sample source 계약 정합성·POSD·DDD 점검

## 현재 판정

runtime source·unit gate 뒤에 sample source 계약 정합성을 수정했다. 현재 source build와 sample
regression은 통과했지만, 실제 sample process E2E, raw wire capture, package-only process, 독립
review는 아직 실행하지 않았다. 따라서 Phase B와 통합 ledger는 완료로 판정하지 않는다.

## 실행 evidence

| 영역 | 명령 | 결과 |
|---|---|---|
| DeliveryDispatch | `dotnet build DeliveryDispatch.sln --no-restore --nologo` | exit 0, 0 warning, 0 error |
| ShoppingMall | `dotnet build ShoppingMall.csproj --no-restore --nologo` | exit 0, 0 warning, 0 error |
| GameQuest | `dotnet build GameQuest.csproj --no-restore --nologo` | exit 0, 0 warning, 0 error |
| Bingo | `dotnet build Bingo.csproj --no-restore --nologo` | exit 0, 0 warning, 0 error |
| ZoneWorld server | `dotnet build Server/ZoneNode/ZoneWorld.Server.ZoneNode.csproj --no-restore --nologo` | exit 0, 0 warning, 0 error |
| ZoneWorld client | `dotnet build Client/ZoneWorld.Client.csproj --no-restore --nologo` | exit 0, 0 warning, 0 error |
| sample source regression | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --no-restore --nologo` | `141/141` passed |

## 수정 결과

- `DS-IMP-001`: TicTacToe의 `LeaveGameMsg`, SupportChat의 `SetTypingMsg`를 shared contract,
  handler, client send path와 runner evidence에서 같은 이름으로 맞췄다. 두 path 모두 request/reply
  handler가 아니라 one-way send handler를 사용한다.
- `DS-IMP-002`: 공통 GameQuest declaration에 없는 `CompleteMission*`와 `UnlockFeature*` public action을
  제거했다. `CollectItemReq`와 `EnterAreaReq`는 response 없는 send handler로 변경했다. `GameplayMsg`
  와 `StoredQuestEvent`의 payload는 `byte[]` 대신 `JsonElement` object를 사용하고, typed conversion은
  `QuestContractMapper`가 소유한다. 공통 GameQuest 문서의 action response 설명도 이 transport 의미와
  일치하도록 고쳤다.
- `DS-IMP-003`: `StartOrderRes`를 `orderId`와 `OrderState state` 형태로 맞췄다. Commerce application
  port의 `Command/Result` DTO는 `Server/Shared/Ports` 내부에 그대로 두고 shared wire contract로
  이동시키지 않았다.
- `DS-IMP-004`: DeliveryDispatch의 세 status message가 `long OccurredAtUnixMs`를 직접 전달하도록
  맞췄다. evidence store와 customer notify도 같은 숫자를 사용한다.
- `DS-IMP-005`: Bingo `AuthenticatePlayerRes`의 세 선택 field를 Protobuf `optional`로 선언했다.
  공통 목록에 없는 `BingoJoinFailedNotify`와 `BingoActorEntrySpotNotify`는 schema와 전송 path에서
  제거하고 actor completion·server log를 failure/evidence 경계로 유지했다.
- `DS-IMP-006`: ZoneWorld에 `UpdatePositionMsg`를 추가했다. same-zone movement는 기존 public
  `IZLinkSpotClient.SendToSpot`으로 message를 보내고, `ZoneSpot`의 packet handler만 projection을
  갱신한다. movement가 internal `ZoneSpot.UpdatePosition`을 직접 호출하지 않는다.
- `DS-IMP-007`·`DS-IMP-008`: sample README의 Framework 기준을 `11.0.0`으로 고쳤고, PowerShell
  integrated runner에 ZoneWorld를 추가했다. `Systems.Zlink` bindings version과 Framework contract
  version은 별도로 유지한다.
- `DS-IMP-009`: sample regression에 exact name·field·transport boundary와 shell·PowerShell 7종
  inventory 검사를 보강했다. 이는 source gate이며 실제 process evidence를 대신하지 않는다.

## POSD·DDD finding

### PDD-DOTNET-003 — GameQuest payload 변환 책임

문제는 transport payload를 각 handler가 bytes로 해석하면 serializer 지식과 domain 지식이 여러
경계로 새는 것이다. `GameplayMsg`와 durable record는 JSON object를 `JsonElement`로 전달하고,
`QuestContractMapper`가 typed domain record로 바꾸도록 했다.

- 원칙: deep module, information hiding, complexity below the boundary; GameApi와 QuestMission을
  각각의 bounded context로 유지한다.
- 대안 A: 기존 `byte[]`를 유지하고 handler마다 `SerializeToUtf8Bytes`·deserialize를 수행한다.
  호출부에 codec 지식이 남으므로 기각했다.
- 대안 B: `object`와 message별 codec registry를 public contract에 추가한다. caller가 serializer를
  선택해야 하고 기본 typed JSON 경로를 우회하므로 기각했다.
- 선택: shared contract는 object shape를 표현하고 mapper 한 곳에서 transport/domain conversion을
  수행한다. domain assembly는 `GameQuest.Shared`, Zlink, JSON 또는 persistence를 참조하지 않는다.

### PDD-DOTNET-004 — ZoneWorld same-zone projection owner

actor가 좌표를 바꾼 뒤 `ZoneSpot` 내부 method를 직접 호출하면 actor turn과 Spot turn의 message 순서가
   보이지 않고, 공통 문서의 `UpdatePositionMsg` 경계도 사라진다.

- 원칙: information hiding, deep module, DDD aggregate ownership. Actor는 authoritative coordinate를
  소유하고 ZoneSpot은 rendering projection을 소유한다.
- 대안 A: 기존 `ZoneSpot.UpdatePosition` direct call을 유지한다. 가장 짧지만 queue·handler·transport
  계약을 우회하므로 기각했다.
- 대안 B: 별도 `PositionProjectionService`를 만들고 actor와 Spot이 같은 service를 호출한다. state
  owner가 분산되고 pass-through abstraction이 생기므로 기각했다.
- 선택: 기존 public `IZLinkSpotClient`로 `UpdatePositionMsg`를 보내고, `IZLinkSpotPacketHandler`가
  `ZoneSpot.ApplyPositionUpdate`를 호출한다. 새 public API나 raw frame은 만들지 않았다.

### PDD-DOTNET-005 — 공통 문서에 없는 public sample action·notify

다른 구현이나 기존 sample source가 더 많은 message를 가진다는 이유로 public contract를 확장하지
않았다. 공통 sample 문서의 formal declaration을 기준으로 `.NET` source를 줄였다.

- 대안 A: 기존 extra action·notify를 유지하고 공통 문서에 추가한다. source가 contract의 근거가 되고
  언어 간 public parity가 임의 구현에 종속되므로 기각했다.
- 대안 B: extra를 internal alias로 남겨 client가 계속 사용하게 한다. public compatibility alias와
  중복 contract fixture가 되어 기각했다.
- 선택: `CompleteMission*`, `UnlockFeature*`, Bingo extra notify를 제거하고, 실제 실패·도착 evidence는
  actor completion과 server file log로 확인한다. contract 변경이 필요한 경우에는 먼저 common 문서를
  정식으로 바꾸는 절차를 적용한다.

### PDD-DOTNET-006 — ShoppingMall port와 wire message 경계

`ReserveInventoryCommand/Result`, `ReleaseInventoryCommand/Result`, `AuthorizePaymentCommand/Result`는
같은 process의 application port DTO다. 이를 `Req/Res`로 이름만 바꾸거나 shared wire contract로 올리면
Commerce API가 workflow 내부 결정을 노출한다.

- 원칙: DDD application port와 transport contract의 bounded-context 경계를 유지하고, POSD의
  information hiding을 적용한다.
- 대안 A: 모든 port DTO를 shared `Req/Res`로 이동한다. wire surface가 불필요하게 늘어나므로 기각했다.
- 대안 B: 같은 의미의 public wrapper를 하나 더 만든다. 중복 contract와 pass-through layer가 생기므로
  기각했다.
- 선택: public `StartOrderRes`만 common shape에 맞추고, in-process `Command/Result`는 기존 port에
  유지한다.

### PDD-DOTNET-007 — GameQuest close 완료와 one-way action ordering

`ClosePlayerQuestMsg` 뒤에 바로 `EnterAreaReq`를 보내면 close 요청의 public 호출이 반환된 시점과
QuestMission owner의 durable close 처리가 끝난 시점이 분리될 수 있다. 그 상태에서는 다음 one-way
action이 아직 닫히지 않은 quest를 기준으로 처리되어, client가 보는 progress ordering과 domain
상태가 달라진다.

- 원칙: DDD aggregate lifecycle invariant는 QuestMission owner가 소유하고, POSD의 temporal
  decomposition을 호출자에게 노출하지 않는다. one-way message는 response를 만들지 않지만, 다음
  contract action이 이전 lifecycle 전이를 관찰해야 하는 경우에는 이미 공개된 typed sync 경계를
  사용해야 한다.
- 대안 A: client가 임의의 delay를 넣는다. 처리 완료를 보장하지 못하고 scheduler timing을 sample
  contract로 밀어내므로 기각했다.
- 대안 B: `ClosePlayerQuestRes`라는 새 public response를 추가하거나 close 내부 상태를 client에
  노출한다. common contract에 없는 surface와 lifecycle detail이 생기므로 기각했다.
- 선택: close one-way semantics는 유지하고, 다음 action 전에 이미 공통 flow에 있는
  `SyncQuestProgressReq`를 typed barrier로 사용한다. QuestMission이 close event와 progress
  projection을 완료한 뒤 barrier 응답을 반환하는 책임을 owner에 두고, client는 그 결과를 확인한
  뒤 `EnterAreaReq`를 보낸다. 새 DTO, raw frame, sleep workaround는 추가하지 않았다.

## 남은 gate

다음 단계는 source 수정 결과를 실제 process에서 실행하며 client self-check, server evidence, message
trace와 file log, cleanup을 모두 확인하는 것이다. 그 뒤 fresh package consumer와 package-only process를
분리해 실행하고, 마지막 독립 audit 전까지 Phase B 완료 checklist를 체크하지 않는다.
