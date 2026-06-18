# GameQuest Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — GameQuest](../../../../../doc/spec/sample/event/gamequest.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다. payload codec은 JSON이다.

## 1. 목적

stateless Game API 서버와 stateful QuestMission 처리 서버를 분리하고, ZLink fanout과 event
sourcing으로 quest·mission 진행을 처리하는 gameplay 샘플이다. client action은 여러 `GameApi`
instance 중 어디로 들어와도 되고, `PlayerId` 기준 owner로 route된 gameplay event를
`PlayerQuestSpot`이 처리한다.

## 2. 서버 구성

`Registry`, `GameApi`(x2), `QuestMission`(x2), `Client`. `GameApi`는 action API와 client session,
gameplay module(combat/inventory/mission/feature/world) 실행, gameplay event publish, quest
notify 전달을 맡는다. `QuestMission`은 gameplay event 구독, `PlayerQuestSpot` owner routing,
quest event append, projection 갱신, reward idempotency를 맡는다. 저장소(`QuestEventStore`,
`QuestReadModelStore`, `GameplayStateStore`, `QuestSubscriptionStore`)는 별도 ZLink 서버가
아니라 각 서버의 dependency로 둔다. Registry/Discovery로 자동 발견한다.

## 3. 전체 흐름

1. client action이 `GameApi` 중 한 instance로 들어온다.
2. `GameApi`의 gameplay module이 `MonsterKilledEvent`, `ItemCollectedEvent`,
   `MissionCompletedEvent`, `FeatureUnlockedEvent`, `AreaEnteredEvent`를 ZLink fanout으로
   publish 한다.
3. `QuestMission`이 event를 구독해 `PlayerId` 기준 `PlayerQuestSpot`에 route 한다.
4. `PlayerQuestSpot`이 `(PlayerId, QuestId)` event stream을 replay해 aggregate를 복원하고,
   조건 평가 후 `QuestProgressedEvent`/`QuestCompletedEvent`/`QuestRewardGrantedEvent`를 append 한다.
5. quest progress는 `QuestReadModelStore` projection으로 조회하고, client에 `QuestProgressNotify`로
   push 한다. projection을 지워도 event replay 만으로 재생성된다.

## 4. 비동기 진행 관용구

gameplay event 적용과 projection 갱신은 Promise 기반 비동기 흐름으로 진행하고, client는
관찰 가능한 상태/notify를 기다린다. readiness를 sleep으로 숨기지 않는다.

## 5. 경계 메모

`GameApi`의 action API와 WebSocket session은 NestJS HTTP/WS 표면으로 두되, gameplay event
전파와 quest 처리는 ZLink fanout과 framework Spot 표면으로 모델링한다. 같은 `PlayerId`의
event 적용은 항상 같은 `PlayerQuestSpot` owner 흐름으로 모인다.

## 6. 호출 표면 (객체-메시징)

```ts
await publisher
  .publishToChannel("gameplay-events", new MonsterKilledEvent(playerId, monsterId))
  .submit();
```

gameplay module은 framework publisher 표면으로 업무 event를 발행하고, codec(JSON)·packet name은
framework 내부가 처리한다.

## 7. Client self-check

같은 quest event를 중복 적용해도 진행이 멱등인지, projection 삭제 후 replay 결과가 같은지,
reward가 한 번만 지급되는지, 어느 `QuestMission` instance에서 조회해도 같은 projection인지를
server-side assertion으로 확인한다.

## 8. 완료 기준

- stateless API scale-out과 `PlayerId` owner routing이 모두 동작한다.
- event-sourcing·idempotency·reward·projection rebuild가 동작한다.
- JSON codec을 쓰고 readiness를 sleep으로 숨기지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
