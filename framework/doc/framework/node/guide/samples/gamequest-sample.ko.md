# GameQuest Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — GameQuest](../../../common/sample/event/gamequest.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다. payload codec은 JSON이다.

## 1. 목적

stateless Game API 서버와 stateful QuestMission 처리 서버를 분리하고, ZLink fanout과 event
sourcing으로 quest·mission 진행을 처리하는 gameplay 샘플이다. 현재 TypeScript 구현은
channel handler와 role service로 quest 진행을 검증하는 compact 구현이다. 공통 시나리오의 최종
목표는 `PlayerQuestSpot` owner 구조지만, 현재 TypeScript 샘플은 아직 Spot factory를 등록하지 않는다.

## 2. 서버 구성

`Registry`, `Server`, `Client`로 구성한다. 현재 TypeScript 구현은 하나의 서버 프로세스 안에서
Game API 역할과 QuestMission 역할을 module로 나누고, `GameApi` 역할은 action API와 client session,
gameplay module(combat/inventory/mission/feature/world) 실행, gameplay event 처리, quest
notify 전달을 맡는다. `QuestMission` 역할은 quest event append, projection 갱신, reward idempotency를
role service와 handler로 처리한다. 저장소(`QuestEventStore`, `QuestReadModelStore`,
`GameplayStateStore`, `QuestSubscriptionStore`)는 별도 ZLink 서버가 아니라 서버의 dependency로
둔다.

## 3. 전체 흐름

1. client action이 `GameApi` 중 한 instance로 들어온다.
2. `GameApi`의 gameplay module이 `MonsterKilledEvent`, `ItemCollectedEvent`,
   `MissionCompletedEvent`, `FeatureUnlockedEvent`, `AreaEnteredEvent`를 ZLink fanout으로
   publish 한다.
3. `QuestMission`이 event를 받아 `(PlayerId, QuestId)` event stream을 replay해 aggregate를 복원한다.
4. 조건 평가 후 `QuestProgressedEvent`/`QuestCompletedEvent`/`QuestRewardGrantedEvent`를 append 한다.
5. quest progress는 `QuestReadModelStore` projection으로 조회하고, client에 `QuestProgressNotify`로
   push 한다. projection을 지워도 event replay 만으로 재생성된다.

## 4. 비동기 진행 관용구

gameplay event 적용과 projection 갱신은 Promise 기반 비동기 흐름으로 진행하고, client는
관찰 가능한 상태/notify를 기다린다. readiness를 sleep으로 숨기지 않는다.

## 5. 경계 메모

`GameApi`의 action API와 WebSocket session은 NestJS HTTP/WS 표면으로 두되, gameplay event
전파와 quest 처리는 TypeScript compact 샘플에서는 channel handler와 role service로 모델링한다.
같은 `PlayerId`를 항상 같은 `PlayerQuestSpot` owner 흐름으로 모으는 구조는 full 구조 승격 때
추가해야 한다.

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
reward가 한 번만 지급되는지 server-side assertion으로 확인한다.

## 8. 완료 기준

- event-sourcing·idempotency·reward·projection rebuild가 동작한다.
- JSON codec을 쓰고 readiness를 sleep으로 숨기지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
