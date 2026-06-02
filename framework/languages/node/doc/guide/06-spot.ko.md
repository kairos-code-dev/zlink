# SPOT — 상태를 가진 실행 단위

Spot 은 room, match, stage 처럼 상태가 있는 단위를 표현한다. Spot 의 핵심은 같은
Spot 안의 작업을 하나의 실행 문맥에서 순서대로 처리한다는 점이다.

## 1. Spot manager

`ZLinkSpotManager` 는 Spot 생성, 조회, 제거를 맡는다.

```ts
const result = await manager.create(GameSpot);
const game = manager.find(GameSpot, result.spotRid);
```

`getOrCreate` 는 Spot 타입과 `spotRid` 를 함께 본다. 같은 `spotRid` 라도 타입이
다르면 같은 Spot 으로 취급하지 않는다.

## 2. outbound

Spot 안에서는 `context.outbound` 를 통해 channel 또는 다른 Spot 으로 보낸다.

```ts
await spot.context.outbound
  .requestToChannel('match.api', { gameId })
  .packetName('LoadMatch')
  .submit();
```

## 3. timer

`context.addTimer(...)` 로 timer 를 등록한다. timer callback 도 Spot 실행 문맥에서
처리되어 Spot 상태를 보호한다.

## 회귀 테스트

Spot lifecycle, serial executor, timer, outbound 는 `test/contract/spot-manager.test.js`
에서 확인한다.
