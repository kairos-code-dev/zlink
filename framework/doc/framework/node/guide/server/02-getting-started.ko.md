<!-- framework-adapter-nav:start -->
[가이드 홈](../../../index.ko.md) | [이전: 1. 개요](01-overview.ko.md) | [다음: 3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md)
<!-- framework-adapter-nav:end -->

# 2. 시작하기

> 이 장이 따라가는 코드는 저장소의 `framework/languages/node/samples/TicTacToe.Ts`다.
> 등록 표면의 정식 계약은
> [Node.js foundation과 configuration 공개 계약](../../../common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md)이
> 다룬다.

두 프로세스가 서버 간 channel로 대화하는 최소 구성을 만든다. API 서버가 요청을 받아
Play 서버에 넘기는 흐름 하나만 본다.

## 1. 패키지 설치

```bash
npm install @zlink-systems/framework @zlink-systems/nestjs
# 여러 node를 쓸 때만.
npm install @zlink-systems/framework-locations-redis
```

**import 출처가 둘로 나뉜다.** 계약 타입(`ZLinkRequestHandler` 등)은
`@zlink-systems/framework`에서, 등록과 데코레이터(`ZLinkModule` · `zlinkRequestHandler`
등)는 `@zlink-systems/nestjs`에서 온다.

## 2. 모듈에 얹기

```typescript
import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';

@Module({
  imports: [
    ZLinkModule.forRootFactory({
      useFactory: () => {
        const builder = zlinkFramework();

        // 이 process가 호출하는 쪽이다 — Play 서버에 연결만 한다.
        const mesh = builder.addRouteMesh('play')
          .listen(config.routeEndpoint)
          .setRoutingIdPrefix('tictactoe-api');
        mesh.channel('play.game').client();
        mesh.peerConnections().connect(config.playEndpoint);

        return builder;   // builder를 돌려줘야 한다.
      }
    }),
    // 이 디렉터리 아래의 handler·Spot·Actor를 찾아 provider로 등록한다.
    zlinkModule(__dirname, { })
  ]
})
export class ApiModule {}
```

**`useFactory`는 builder를 돌려준다.** 등록만 하고 `return`을 빼면 아무것도 켜지지
않는다. 다른 언어의 "options를 받아 등록하고 끝"과 다른 자리다.

`zlinkModule(__dirname, ...)`은 그 디렉터리를 훑어 데코레이터가 붙은 class를 Nest
provider로 모아 준다. 직접 `providers`에 나열해도 된다.

## 3. 받는 쪽 — handler

handler는 계약 interface를 구현하고 데코레이터로 group과 packet 이름을 밝힌다.

```typescript
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';

@zlinkRequestHandler('play', PacketNames.createGame)
export class CreateGameHandler
  implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {

  constructor(private readonly games: GameStore) {}   // Nest DI로 주입된다.

  async handle(request: CreateGameReq): Promise<CreateGameRes> {
    const game = await this.games.create(request.gameName);
    return createGameRes(game.id);
  }
}
```

**첫 인자가 handler group, 둘째가 packet 이름이다.** packet 이름은 보내는 쪽과 정확히
같아야 한다 — 상수로 묶어 공유한다.

Play 서버는 같은 channel을 **server**로 열고 handler group을 붙인다.

```typescript
const mesh = builder.addRouteMesh('play')
  .listen(config.routeEndpoint)
  .setRoutingIdPrefix('tictactoe-play');
mesh.channel('play.game').server()
  .addHandlerGroup('play');
```

## 4. 보내는 쪽 — 토큰으로 주입

client는 **주입 토큰으로 받는다.** 타입만 적어서는 Nest가 무엇을 넣을지 모른다.

```typescript
import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import type { ZLinkRouteClient } from '@zlink-systems/framework';

@Controller()
export class GameController {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly client: ZLinkRouteClient
  ) {}

  @Post('/games')
  async create(@Body() request: CreateGameReq): Promise<CreateGameRes> {
    return this.client
      .requestToChannel('play.game', request)
      .timeout(3_000)
      .submit<CreateGameRes>();
  }
}
```

주입 토큰은 표면마다 있다 — `ZLINK_ROUTE_CLIENT` · `ZLINK_CHANNEL_CLIENT` ·
`ZLINK_FANOUT_CLIENT` · `ZLINK_ACTOR_CLIENT` · `ZLINK_ACTOR_MANAGER` ·
`ZLINK_FRAMEWORK_RUNTIME` 등이다. 목록은
[13. 주요 interface 사용 색인](13-interface-catalog.ko.md)에 있다.

`timeout(...)`은 **밀리초 숫자**다. 다른 언어의 `Duration` 타입과 다르다.

## 5. 실행과 확인

```bash
framework/languages/node/samples/TicTacToe.Ts/run_sample.sh
```

runner가 서버 여러 개와 client 시나리오를 함께 띄우고 검증까지 한다. Redis가 필요한
샘플은 runner가 컨테이너를 직접 띄우고 끝나면 정리하므로 `docker`만 있으면 된다.

확인 순서는 셋이다.

1. **기동 로그** — 등록이 잘못되면 첫 호출까지 미루지 않고 모듈 초기화에서 실패한다.
2. **client의 exit code** — 시나리오 성공 여부의 판정 기준이다.
3. **서버 로그의 dispatch 오류** — client가 통과해도 서버가 오류를 기록할 수 있다.

## 6. 다음에 볼 것

| 하려는 것 | 볼 장 |
| --- | --- |
| 개념을 먼저 잡기 | [3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md) |
| 요청 방식 세 가지 | [5. Channel Messaging](../../../common/guide/server/05-channel-messaging.ko.md) |
| 방·세션 상태를 담기 | [6. Spot](../../../common/guide/server/06-spot.ko.md) |
| client 실시간 연결 | [9. STREAM](../../../common/guide/server/09-stream.ko.md) |
| 설정 값 목록 | `16. Options` 장 |

## 7. 관련 문서

- 정식 계약: [Node.js foundation과 configuration 공개 계약](../../../common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md)
- 이전 장: [1. 개요](01-overview.ko.md)
- 다음 장: [3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md)
- 샘플 전체: [14. 샘플 고르기](../../../common/guide/server/14-samples.ko.md)
