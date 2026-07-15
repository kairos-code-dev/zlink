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

## 4. CPU 작업과 I/O 작업

Spot handler에서 오래 걸리는 계산은 `context.runCpuWorker(...)`로 실행한다. 이 함수에 넘기는
작업은 격리된 worker thread에서 실행되므로 외부 변수를 참조하지 않는 독립 함수여야 한다.
반환값도 structured clone으로 복사할 수 있어야 한다.

```ts
const score = await context.runCpuWorker(() => {
  // 계산은 Spot 상태나 handler의 외부 변수를 참조하지 않는다.
  return Array.from({ length: 10_000 }, (_, index) => index).reduce((sum, value) => sum + value, 0);
}).submit();
this.currentScore = score; // 완료 뒤 Spot 실행 문맥에서 상태를 갱신한다.
```

네트워크나 파일처럼 Promise를 기다리는 작업은 CPU worker thread를 점유하지 않도록
`context.runIoWorker(...)`를 사용한다.

```ts
const profile = await context.runIoWorker(async (signal) => {
  // I/O 취소 신호를 실제 요청에도 전달한다.
  const response = await fetch(profileUrl, { signal });
  return response.json() as Promise<PlayerProfile>;
}).yield();
this.profile = profile; // 재개된 Spot 실행 문맥에서 결과를 반영한다.
```

## 5. 비동기 handler의 실행 순서

비동기 호출을 시작한 뒤 `.submit()`을 선택하면 현재 실행 턴을 유지한다. 완료를 기다리는 동안
같은 직렬 실행 경계의 다음 작업은 시작되지 않으므로, 완료 전후의 Spot 상태를 한 작업으로
보호해야 할 때 사용한다.

`.yield()`를 선택하면 기다리는 동안 현재 실행 턴을 반납한다. 같은 Spot의 다른 작업을 처리할 수
있고, 비동기 호출이 끝난 뒤 continuation이 다시 실행 큐에 들어간다. 따라서 기다리는 동안 다른
handler가 바꿀 수 있는 Spot 상태를 완료 뒤에도 그대로라고 가정하면 안 된다. 한 호출에서는
`.submit()`과 `.yield()` 중 하나만 선택한다.

## 회귀 테스트

Spot lifecycle, serial executor, timer, outbound 는 `test/contract/spot-manager.test.js`
에서 확인한다.
