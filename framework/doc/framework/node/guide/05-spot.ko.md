# SPOT — 상태를 가진 실행 단위

## 현재 구현 기준

외부 channel에서 특정 Spot으로 send/request를 보낼 때는 framework가 core
`SpotRouteBridge`를 내부에서 사용한다. 사용자는 SpotMesh와 RouteMesh channel만 등록하면
되고, raw `DEALER`, `ROUTER`, `PUB` socket을 `SpotNode`에 직접 attach하지 않는다. Spot에서 외부 pub/sub channel로 publish할 때는 일반 channel
publisher client를 주입해서 사용한다.

Spot 은 room, match, stage 처럼 상태가 있는 단위를 표현한다. Spot 의 핵심은 같은
Spot 안의 작업을 하나의 실행 문맥에서 순서대로 처리한다는 점이다.

## 1. Spot manager

`ZLinkSpotManager` 는 Spot 생성, 조회, 정상 종료를 맡는다. NestJS 앱에서는 먼저
`SpotNode` 와 그 노드가 만들 수 있는 Spot 클래스를 함께 등록한다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .addSpotMesh('game')
      .addSpotFactory(GameSpot)
    .build()
);
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
class LoadMatchReq {
  constructor(readonly gameId: string) {}
}

await spot.context.outbound
  .requestToChannel('match.api', new LoadMatchReq(gameId))
  .submit();
```

## 3. timer

`context.addTimer(...)` 로 timer 를 등록한다. timer callback 도 Spot 실행 문맥에서
처리되어 Spot 상태를 보호한다.

SPOT handler 등록도 자동 등록을 기본으로 권장한다. Bingo.Ts 샘플은
`@zlinkSpotActorRequestHandler(...)` 와 `@zlinkSpotTimerHandler(...)` 를
`zlinkDiscoverProviders(...)` 로 provider 에 넣어 자동 등록 경로를 보여 준다.
timer decorator 는 이름과 주기가 함께 있을 때 자동 schedule 로 연결된다. 이름과
주기를 생략한 decorator 는 provider discovery 표시로만 사용하고, schedule 은
Spot 코드에서 `context.addTimer(...)` 로 직접 등록한다.

수동 등록은 Spot 또는 Entry Spot 의 `configure()` 에서 한다. `configure()` 는
동기 함수와 `Promise<void>` 를 모두 허용하며, runtime 은 초기화 전에 이 작업이
끝날 때까지 기다린다. TicTacToe.Ts 샘플은 Entry Spot actor request, user Spot
actor request, timer 를 이 방식으로 등록한다.

```ts
class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context?: ZLinkEntrySpotContext;

  configure(): void {
    this.context!.handlers.actorRequest('JoinGame', PlayActorJoinGameHandler);
  }
}

class GameSpot implements ZLinkSpot {
  readonly context?: ZLinkSpotContext;

  async configure(): Promise<void> {
    this.context!.handlers.actorRequest('PlaceMark', PlaceMarkHandler);
    await this.context!.addTimer('game-tick', 1000, GameTimerHandler);
  }
}
```

`context.handlers.packet(...)` 과 `context.handlers.subscribe(...)` 도 등록 표면에
있다. 현재 가이드 예시는 실제 샘플에서 사용하는 actor request 와 timer 중심으로
보여 준다.

## 4. worker deferral

짧은 local 작업을 Spot 실행 문맥 밖으로 잠시 넘겨야 하면 `context.runWorker(...)`
를 사용한다. worker 함수는 Spot 상태를 직접 바꾸지 않고, 완료 callback 에서 Spot
실행 문맥으로 돌아온 뒤 상태를 갱신한다.

```ts
context.runWorker(() => calculateScore(snapshot))
  .onCompleted((score) => {
    this.currentScore = score;
  });
```

Node.js 의 `runWorker(...)` 는 closure 를 `worker_threads` 로 옮겨 실행한다고
보장하지 않는다. 오래 걸리는 CPU 작업이나 재시도가 필요한 작업은 별도 ZLink
service/server 로 요청한다.

## 5. 비동기 handler의 실행 순서

Spot handler가 반환한 Promise가 끝날 때까지 같은 직렬 실행 경계의 다음 작업은 시작되지
않는다. 따라서 `await` 전후에 같은 상태를 읽고 수정해도 별도 public turn 반납 API가 필요하지
않다. 오래 걸리는 CPU 작업은 별도 worker 또는 service로 분리하고, `AsyncLocalStorage`는
logging이나 request context 용도로만 사용한다.

## 회귀 테스트

Spot lifecycle, serial executor, timer, outbound 는 `test/contract/spot-manager.test.js`
에서 확인한다.
