<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [다음: Getting Started](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework for Kotlin Overview

## 1. 한 줄 정의

`ZLink Framework for Kotlin`은 Java `zlink-framework` 런타임 위에 올라가, 별도 gateway나
전용 로드밸런서 없이 논리 `channel name` 기준 서버 간 호출, pub/sub, `SPOT`,
`STREAM`을 Spring Boot bean lifecycle과 DI 모델 안에서 **coroutine 표면으로** 쓰게 해
주는 상위 계층이다.

개발자는 low-level socket, discovery, reconnect, correlation을 직접 다루지 않는다.
handler와 outbound client만 작성하고, 연결과 라우팅은 framework가 처리한다. handler는
`suspend` 함수로 쓰며, 응답을 기다리는 동안 스레드를 park하지 않는다.

## 2. 아키텍처

```text
+----------------------------------------------------------+
| Spring Boot app (bean, DI, lifecycle, suspend handler)   |
+----------------------------------------------------------+
| ZLink Framework for Kotlin (coroutine idiom layer)       |
| - suspend handler - Flow stream - awaitReply 확장        |
+----------------------------------------------------------+
| ZLink Framework for Java (channel/SPOT/actor/stream)     |
+----------------------------------------------------------+
| zlink Java binding                                       |
+----------------------------------------------------------+
| zlink core (C API) - transport, ZMP, I/O threads        |
+----------------------------------------------------------+
```

framework는 새 transport를 만들지 않는다. `zlink-framework-kotlin`은 Java framework가
노출하는 같은 channel·Spot·actor·stream 위에 `suspend`/`Flow` 표면만 얹는다. 그래서
기능과 의미론(lifecycle, 실패 모델, 등록 규칙)은 Java framework를 정본으로 공유하고
([spec](../../spec/server/languages/java/README.ko.md)·[runtime internals](../../java/internals/runtime-lifecycle.ko.md)),
이 guide는 Kotlin 사용 표면만 다룬다.

### zlink core 와 기본 socket 패턴

위 layer 그림처럼 framework 는 직접 socket을 열지 않는다. zlink core(C API)가 socket 패턴을
제공하고, Java 바인딩이 이를 typed 클래스로 노출하며, framework 가 channel·spot 으로
감싼다. 그래서 가이드 곳곳에 `DEALER`·`ROUTER`·`PUB/SUB` 이름이 보이며, 어떤 socket 위에서
도는지 알면 channel 종류 선택이 쉬워진다.

| framework 구성 | 하부 socket | 쓰임 |
|----------------|-----------|------|
| client-server channel | `DEALER → ROUTER` | 1:1 request/response·단방향 send |
| fanout channel | `PUB → SUB` | 이벤트 fan-out (여러 구독자) |
| mesh channel | `DEALER`/`ROUTER` peer mesh | 로드밸런싱·엔티티 라우팅 |
| STREAM session | `STREAM` | 외부 client(raw TCP/WS) 연동 |

각 socket의 메시징 패턴·라우팅 전략·호환성 매트릭스·코드 예제는 zlink core 가이드가
자세히 다룬다:
[socket 패턴 개요](../../../../../core/doc/guide/03-0-socket-patterns.ko.md) ·
[DEALER](../../../../../core/doc/guide/03-3-dealer.ko.md) ·
[ROUTER](../../../../../core/doc/guide/03-4-router.ko.md) ·
[PUB/SUB](../../../../../core/doc/guide/03-2-pubsub.ko.md) ·
[STREAM](../../../../../core/doc/guide/03-5-stream.ko.md)

## 3. 통합 축

| 축 | Kotlin 사용자에게 보이는 것 |
|----|----------------------|
| channel messaging | `ZLinkSuspendingRequestHandler`, `client.request<R>(...)` |
| fanout | `ZLinkSuspendingPublishHandler`, `fanout.publishToTopic(...)` |
| Spot | `ZLinkSuspendingSpot<TActor>`, timer, outbound |
| actor/session | actor factory, Entry Spot, `ZLinkBoundSession` |
| STREAM | `ZLinkSuspendingSession`, Stream Connector + `Flow` |
| Registry | embedded registry, topology query |
| Monitoring | runtime event handler (`ZLinkRuntimeEventHandler`) |

coroutine handler를 켜는 한 줄(`useCoroutineHandlers(dispatcher)`)과 첫 request는
[02-getting-started](02-getting-started.ko.md)에서 다룬다. Java 표면과의 1:1 대응표는
[kotlin README §0](../README.ko.md#0-kotlin-표면-한눈에)에 있다.

## 4. 현재 상태

이 guide는 현재 `zlink-framework-kotlin` 구현과 Kotlin sample release gate를 기준으로
작성한다. 공개 계약의 정확한 시그니처는 Java/Kotlin 공유 spec
[인터페이스 카탈로그](../../spec/server/languages/java/02-handler-interfaces.ko.md)를 기준으로 보고, `.NET`과
Java/Kotlin 표면을 대조해야 할 때는
[runtime lifecycle](../../java/internals/runtime-lifecycle.ko.md)을 함께 읽는다.

## 5. 읽는 순서

1. [02-getting-started](02-getting-started.ko.md)
2. [03-concepts](03-concepts.ko.md)
3. [10-feature-map](10-feature-map.ko.md)
4. [04-channel-messaging](04-channel-messaging.ko.md)
5. [05-spot](05-spot.ko.md)
6. [06-actor-session](06-actor-session.ko.md)
7. [07-stream](07-stream.ko.md)
8. [08-registry](08-registry.ko.md)
9. [09-monitoring](09-monitoring.ko.md)
10. [11-interface-catalog](11-interface-catalog.ko.md)
11. [12-grpc-alternative](12-grpc-alternative.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [다음: Getting Started](02-getting-started.ko.md)
<!-- framework-adapter-nav:bottom:end -->
