---
title: "1. 개요 · Node/TypeScript"
---

<!-- generated:start -->
<!-- 이 파일은 `common/guide/server/01-overview.ko.md`에서 생성한다. 직접 고치지 않는다.
     고칠 곳은 공통 소스이고, `python3 doc/site/scripts/generate_language_guides.py`로 다시 만든다. -->
<!-- generated:end -->

<!-- framework-adapter-nav:start -->
[가이드 홈](README.ko.md) | [다음: 2. 시작하기](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

<!-- language-switch:start -->
다른 언어로 보기 — [C#/.NET](../../../dotnet/guide/server/01-overview.ko.md) · [C++](../../../cpp/guide/server/01-overview.ko.md) · [Java](../../../java/guide/server/01-overview.ko.md) · [Kotlin](../../../kotlin/guide/server/01-overview.ko.md) · **Node/TypeScript**
<!-- language-switch:end -->

# 1. 개요

> **이 장의 계약 소유 문서** — [Framework 개요](../../../common/spec/02-overview.ko.md)가
> 무엇을 제공하는지를, [언어별 공개 계약 목차](../../../common/spec/server/languages/README.ko.md)가
> 각 언어 표면의 정확한 계약을 소유한다. 이 문서는 그 가운데 **어디서 시작하는지**를
> 정리한다.

## 1. 무엇을 만드는가

실시간 메시징이 중요한 서버 시스템을 여러 프로세스로 나눠 만든다. 서버 간 typed
메시징, 상태 단위(Spot)의 직렬 실행, 외부 client 실시간 연결, 무중단 이전을 한 선언
모델 위에서 조합한다.

Node에서는 **NestJS 애플리케이션 안에 얹는다.** 별도 프로세스가 아니라 같은 Node
런타임에서 Nest의 DI·모듈·수명주기를 그대로 쓴다.

## 2. 무엇을 대체하나

| 지금 쓰는 것 | ZLink가 대신하는 부분 |
| --- | --- |
| 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
| 방·세션 상태를 담는 Redis 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
| `socket.io` 세션 관리 코드 | **STREAM session**과 Actor binding |
| 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

HTTP는 대체하지 않는다. 외부 진입은 Nest controller가 그대로 맡고, ZLink는 그 뒤의
서버 간 통신과 상태 처리를 맡는다.

**어떤 상황에서 후보가 되는지**와 gRPC · Orleans · Akka와의 비교는
[17. ZLink를 어디에 쓰나](17-alternative.ko.md)가 다룬다.

## 3. 산출물

| 패키지 | 언제 넣나 |
| --- | --- |
| `@zlink-systems/framework` | 항상. 계약과 런타임 |
| `@zlink-systems/nestjs` | NestJS에 얹을 때. 대부분 함께 넣는다 |
| `@zlink-systems/framework-locations-redis` | 여러 node를 쓸 때. Redis location store |
| `@zlink-systems/framework-codec-protobuf` · `-codec-msgpack` | 기본 JSON 대신 다른 형식을 쓸 때 |
| `@zlink-systems/stream-connector` | client 쪽 실시간 연결. 서버에는 필요 없다 |
| `@zlink-systems/stream-wire` | connector가 쓰는 wire 계층 |
| `@zlink-systems/http-client` | HTTP 요청을 보내는 쪽 |

**계약 타입은 `@zlink-systems/framework`에서, 등록과 데코레이터는
`@zlink-systems/nestjs`에서 온다.** import 출처가 둘로 나뉘는 것이 Node의 특징이다.

설치 절차와 최소 예제는 [2. 시작하기](02-getting-started.ko.md)가 다룬다.

## 4. 등록 진입점

`ZLinkModule.forRootFactory(...)`가 등록을 받고, `zlinkFramework()`가 builder를 만든다.

```typescript
import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';

@Module({
  imports: [
    ZLinkModule.forRootFactory({
      useFactory: () => {
        const builder = zlinkFramework();

        const mesh = builder.addRouteMesh('play')
          .listen(config.meshEndpoint)
          .setRoutingIdPrefix('play');
        mesh.objects().server().addEntrySpot(PlayEntrySpot);

        return builder;
      }
    }),
    // 이 디렉터리 아래의 handler·Spot·Actor를 provider로 모아 준다.
    zlinkModule(__dirname, { })
  ]
})
export class PlayModule {}
```

**`useFactory`는 builder를 돌려준다.** 등록만 하고 끝내는 것이 아니라 마지막에
`return builder`가 있어야 한다.

**handler는 데코레이터로 등록한다.** Node handler는 interface를 구현하고 데코레이터로 어느 group·packet인지 밝힌다.

```typescript
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';

@zlinkRequestHandler('api', PacketNames.getProfile)
export class GetProfileHandler
  implements ZLinkRequestHandler<GetProfileReq, GetProfileRes> {

  async handle(request: GetProfileReq): Promise<GetProfileRes> {
    return getProfileRes(request.accountId);
  }
}
```

데코레이터는 받는 것마다 하나씩 있다 — `zlinkRequestHandler` · `zlinkSendHandler` ·
`zlinkPublishHandler` · `zlinkSpotPacketHandler` · `zlinkSpotSubscriptionHandler` ·
`zlinkSpotTimerHandler` · `zlinkSpotActorSendHandler` · `zlinkSpotActorRequestHandler` ·
`zlinkEntrySpot*Handler` 넷이다. 목록은
[13. 주요 interface 사용 색인](13-interface-catalog.ko.md)에 있다.

## 5. 읽는 순서

이 가이드의 03~17장은 **다섯 언어가 같은 정본에서 생성된다.** 예제는 이 언어의 코드만
담기며 다른 언어 코드가 섞이지 않는다. 읽는 순서는 이 언어의 가이드 진입점이 제시한다.

먼저 [3. 핵심 개념](03-concepts.ko.md)에서 channel · Spot · Actor · stream ·
relocation 다섯 개념을 잡는다. 나머지 장은 그 조합이다.

## 6. 도입 순서 고르기

전부 한 번에 쓰지 않는다. 지금 겪는 문제부터 고른다.

| 지금 겪는 문제 | 먼저 볼 장 |
| --- | --- |
| 서비스가 어디 있는지 관리하기 번거롭다 | [5. Channel Messaging](05-channel-messaging.ko.md) |
| 방·세션 상태에 락이 얽힌다 | [6. Spot](06-spot.ko.md) |
| client 실시간 연결을 직접 관리한다 | [9. STREAM](09-stream.ko.md) |
| 배포할 때 세션이 끊긴다 | [10. Location](10-location.ko.md) · [7. Actor와 Spot](07-actor-spot.ko.md) |
| 부하가 몰릴 때 동작을 모르겠다 | [4. Backpressure](04-backpressure.ko.md) |

## 7. 관련 문서

- 읽는 순서: [Node.js 가이드 진입점](README.ko.md)
- Node 공개 계약: [exact interface 목차](../../../common/spec/server/languages/node/interfaces/README.ko.md)

- 언어 중립 정의: [공통 스펙 목차](../../../common/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
