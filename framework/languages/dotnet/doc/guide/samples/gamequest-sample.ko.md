<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: ShoppingMallCheckout Sample](./shoppingmall-checkout-sample.ko.md) | [다음: ZLink Framework .NET Behavior Matrix](../../internals/behavior-matrix.ko.md)
<!-- framework-adapter-nav:end -->

# GameQuest Sample

[.NET 묶음](../../README.ko.md) | [channel](../../spec/aspnet-core-channel-messaging.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md) | [Registry](../../spec/aspnet-core-registry.ko.md)

> 이 문서는 실행 가능한 GameQuest 샘플 설명이다. 게임 백엔드에 ZLink 를 도입할지
> 판단하려면 [15-case-realtime-game](../case-studies/15-case-realtime-game.ko.md)을 먼저
> 보고, 이 문서에서는 DTO, 서버 구조, 실행 흐름을 확인한다. 언어 중립 정본 시나리오는
> [spec/sample/event/gamequest](../../../../../doc/spec/sample/event/gamequest.ko.md)이 다룬다.

## 1. 목적

GameQuest 는 stateless Game API 서버와 stateful QuestMission 처리 서버를 분리하고,
ZLink fanout 과 event sourcing 으로 quest·mission 진행을 처리하는 gameplay 샘플이다.
TicTacToe/Bingo 가 실시간 방 루프를 맡는다면, GameQuest 는 **여러 gameplay 영역에서 생긴
event 를 player 단위로 누적해 quest 진행을 결정** 하는 다른 게임 백엔드 난제를 맡는다.

이 샘플이 한 번에 보여 주는 것:

- `GameApi` 의 gameplay action 경로(`/combat/kill`, `/inventory/collect`,
  `/mission/complete`, `/world/enter`, `/feature/unlock`)가 단일 gameplay service 를 거쳐
  gameplay event 를 ZLink fanout 으로 publish 한다.
- `QuestMission` 서버가 여러 event type 을 구독하고 `PlayerId` 기준 `PlayerQuestSpot`[^spot]
  owner 로 route 한다.
- owner 로 route 된 event 는 `QuestEventProcessor` 가 현재 projection 과 source-event
  dedup 을 읽어 progress/completion/reward event 로 append 한다(reward 는 idempotent).
  `(PlayerId, QuestId)` event stream replay 로 projection 을 다시 만드는 경로는 self-check
  에서 검증한다.
- fanout event 누락 가능성은 gameplay snapshot 재동기화로 보정하고, 결과도 quest domain
  event 로 남긴다.
- client 는 `GameApi` WebSocket 으로 quest progress notify 를 받는다.

stateless API scale-out 과 player owner routing 을 함께 보이기 위해 `GameApi` 와
`QuestMission` 을 각각 2 instance 로 실행한다. payload codec 은 JSON 을 쓴다.

## 2. 샘플 구성

public DTO[^dto] 는 현재 코드 기준으로
`framework/languages/dotnet/samples/GameQuest/Shared/Messages.cs`(namespace
`GameQuest.Shared`)를 따른다.

client action 과 quest 조회 DTO:

```csharp
public sealed record KillMonsterReq(string PlayerId, string MonsterId, string AreaId, string IdempotencyKey);
public sealed record CollectItemReq(string PlayerId, string ItemId, int Count, string IdempotencyKey);
public sealed record CompleteMissionReq(string PlayerId, string MissionId, string IdempotencyKey);
public sealed record EnterAreaReq(string PlayerId, string AreaId, string IdempotencyKey);
public sealed record UnlockFeatureReq(string PlayerId, string FeatureId, string IdempotencyKey);

public sealed record SubscribeQuestReq(string PlayerId);
public sealed record SubscribeQuestRes(QuestProgress[] ActiveQuests);
public sealed record SyncQuestProgressReq(string PlayerId);

public sealed record QuestProgress(
    string PlayerId,
    string QuestId,
    string Status,
    int CurrentCount,
    int RequiredCount,
    string? LastEventId,
    long UpdatedAtUnixMs);
```

fanout event 와 quest domain event DTO:

```csharp
public sealed record GameplayEventEnvelope(
    string EventId,
    string PlayerId,
    string IdempotencyKey,
    string EventType,
    string Value,
    int Count,
    string SourceApi,
    long CreatedAtUnixMs);

public sealed record QuestProgressedEvent(
    string EventId, string PlayerId, string QuestId,
    int Delta, int CurrentCount, int RequiredCount, string SourceEventId);

public sealed record QuestCompletedEvent(
    string EventId, string PlayerId, string QuestId, string SourceEventId, long CompletedAtUnixMs);

public sealed record QuestRewardGrantedEvent(
    string EventId, string PlayerId, string QuestId, string SourceEventId, string RewardId, long GrantedAtUnixMs);

public sealed record QuestProgressReconciledEvent(
    string EventId, string PlayerId, string QuestId, int CurrentCount, string Reason, long ReconciledAtUnixMs);

public sealed record StoredQuestEvent(
    string EventId, string? SourceEventId, string PlayerId, string QuestId,
    string EventType, byte[] Payload, long Version, long CreatedAtUnixMs);
```

push 와 session binding DTO:

```csharp
public sealed record QuestProgressNotify(string PlayerId, string? TargetConnectionId, QuestProgress Progress);
public sealed record QuestCompletedNotify(string PlayerId, string? TargetConnectionId, QuestProgress Progress, bool RewardGranted);

public sealed record BindQuestSessionReq(string PlayerId, string ConnectionId, string GameApiInstanceId);
public sealed record GetGameplaySnapshotReq(string PlayerId);
```

`PlayerId`/`QuestId`/`EventId` 는 명시적 domain id 다. action req 의 `IdempotencyKey` 와
quest event 의 `SourceEventId` 가 중복 적용과 reward 중복 지급을 막는다.

## 3. 서버 구성

| 서버 | instance | 책임 |
|------|:--------:|------|
| `GameApi` | 2 | HTTP action API, WebSocket session, gameplay module 실행, event publish, quest notify 전달 |
| `QuestMission` | 2 | gameplay event 구독, `PlayerQuestSpot` owner routing, quest event append, projection 갱신 |
| `Registry` | 1 | endpoint discovery |

저장소는 두 개의 file 기반 store 클래스로 구현되며 각 서버가 나눠 소유한다.

| store (소유 서버) | 책임 |
|-------------------|------|
| `QuestStore` (QuestMission) | `(PlayerId, QuestId)` quest domain event stream + 조회·notify 용 projection |
| `GameQuestStore` (GameApi) | gameplay event·snapshot(누적 fact), player 별 WebSocket subscription binding, gameplay 측 projection |

`QuestMission` 서버는 DDD/hexagonal 구조를 따른다.

```text
Server/QuestMission/
  Domain/              # quest aggregate, 조건 평가, domain event 생성
  Application/         # event 적용, projection, reconcile use case
  Adapters/
    ZLink/
      Spots/           # PlayerQuestSpot
    Store/             # event store / read model / gameplay state adapter
```

가장 중요한 규칙은 같은 `PlayerId` 의 gameplay event 를 적용하는 `PlayerQuestSpot` owner 가
하나로 정해져야 한다는 점이다. 같은 player 의 event 처리, domain event append, projection
update 는 같은 owner 흐름으로 모인다.

## 4. 실행 흐름

- **gameplay → quest 진행**: client action(`KillMonsterReq` 등) → `GameApi` gameplay
  service 가 `GameplayEventEnvelope` 를 fanout publish → `QuestMission` 이 구독해 `PlayerId`
  owner(`PlayerQuestSpot`)로 route → `QuestEventProcessor` 가 현재 projection 과
  source-event dedup 을 읽어 조건 평가 →
  `QuestProgressedEvent`/`QuestCompletedEvent`/`QuestRewardGrantedEvent` append 및 projection 갱신.
- **notify**: projection 변화는 player WebSocket 을 소유한 `GameApi` instance 로 subscription
  binding 을 따라 `QuestProgressNotify`/`QuestCompletedNotify` 가 전달된다.
- **누락 보정**: fanout event 누락이 의심되면 `SyncQuestProgressReq` →
  `GetGameplaySnapshotReq` 로 gameplay snapshot 을 재동기화하고
  `QuestProgressReconciledEvent` 로 결과를 남긴다.

## 5. 완료 기준 / self-check

- `QuestMission` 은 event publisher 의 물리 endpoint 를 모른 채 여러 gameplay event type 을
  fanout 으로 구독한다.
- 같은 `PlayerId` 의 event 는 항상 같은 `PlayerQuestSpot` owner 흐름에서 처리된다.
- quest event stream replay 만으로 quest 진행과 reward 지급 여부를 재계산할 수 있다
  (self-check 의 projection rebuild 로 검증).
- reward 는 `SourceEventId` 기준으로 중복 지급되지 않는다.
- snapshot 재동기화 결과가 quest domain event 로 남는다.
- client 는 `GameApi` WebSocket 으로 quest progress notify 를 받는다.

[^spot]: `SPOT` 은 동적으로 생성ㆍ소멸되는 논리적 노드(예: player quest, room 등) 단위로 메시지를 라우팅하는 추상이다.
[^dto]: DTO(Data Transfer Object) 는 컴포넌트 사이에서 데이터를 옮기기 위해 정의한 단순 데이터 클래스를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: ShoppingMallCheckout Sample](./shoppingmall-checkout-sample.ko.md) | [다음: ZLink Framework .NET Behavior Matrix](../../internals/behavior-matrix.ko.md)
<!-- framework-adapter-nav:bottom:end -->
