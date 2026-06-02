# SPOT — 상태를 가진 실행 단위

Spot 은 room, match, stage 처럼 상태가 있는 단위를 표현한다. Spot 의 핵심은 같은
Spot 안의 작업을 하나의 실행 문맥에서 순서대로 처리한다는 점이다.

## 1. Spot manager

`ZLinkSpotManager` 는 Spot 생성, 조회, 제거를 맡는다. NestJS 앱에서는 먼저
`SpotNode` 와 그 노드가 만들 수 있는 Spot 클래스를 함께 등록한다.

```ts
ZLinkModule.forRoot({
  spotNodes: ['game'],
  spotFactories: [GameSpot],
});
```

`ZLINK_SPOT_MANAGER` provider 는 `SpotNode` 가 있을 때만 등록된다. 등록한 Spot
factory 는 module 이 만든 manager 로 전달되므로, sample 처럼 framework class 를
직접 조립하지 않고도 Spot 을 만들 수 있다.

```ts
const result = await manager.create(GameSpot);
const game = await manager.find(result.spotRid);
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
