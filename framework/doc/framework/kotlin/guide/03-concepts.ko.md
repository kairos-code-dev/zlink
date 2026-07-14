<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Getting Started](02-getting-started.ko.md) | [다음: Channel Messaging](04-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin Concepts

## 1. Channel

channel은 논리 호출 이름이다. application은 endpoint 대신 channel name을 기준으로
request, send, publish를 호출한다.

| Channel 종류 | 용도 |
|--------------|------|
| client/server | 일반 request/send |
| fanout | pub/sub event |
| route mesh | target `RoutingId`를 지정하는 routed channel |

## 2. Capability

역할은 한 channel이나 SpotNode가 수행하는 역할이다. 같은 역할 안에서
discovery와 manual connection을 섞지 않는다.

예:

- `price.server`
- `price.client`
- `event.publisher`
- `event.subscriber`
- `play.router`
- `play.pubsub`

## 3. Handler 노출

handler scan은 후보를 찾는 단계다. 실제 노출은 channel 등록(`addHandlerGroup(...)`
등)에서 정한다. framework는 scan된 handler를 모든 channel에 자동으로 열지 않는다.

## 4. Spot

Spot은 room, stage, zone처럼 동적으로 생성되는 논리 노드다. Kotlin은 Spot type
factory, Entry Spot, timer, actor dispatch를 Java와 같은 의미로 제공하며, Spot 베이스
클래스 `ZLinkSuspendingSpot<TActor>`로 lifecycle 콜백을 `suspend`로 작성한다.

## 5. STREAM과 Connector

server 쪽은 `ZLinkSuspendingSession` 하나를 기준으로 dispatch context 기반 packet을 받는다. 외부
client는 server framework에 의존하지 않는 `ZLinkStreamConnector` 모듈을 사용하며,
Kotlin에서는 `connector.kotlin()`으로 coroutine·`Flow` 표면을 얻는다.

## 6. coroutine 실행 모델

framework는 native I/O 스레드에서 콜백을 호출하고, coroutine 레이어가 그 콜백을
`suspend` 함수로 잇는다. handler 안에서 `await`/`request<T>`로 다른 호출을 기다리는
동안 native 스레드는 park되지 않는다. **handler 안에서 blocking 호출
(`Thread.sleep`, blocking JDBC, `CompletableFuture.join` 등)을 직접 쓰지 않는다.**
blocking이 불가피하면 `withContext(Dispatchers.IO)`로 옮긴다. 공통 의미는
[비동기 실행과 coroutine 정책](../../spec/04-async-execution-policy.ko.md)을 따른다.

## 7. Spring DI

Spring bean 주입은 역할 가능성을 암시해야 한다. SpotNode가 없으면
`ZLinkSpotManager`를 등록하지 않고, actor factory가 없으면 `ZLinkActorManager`를
등록하지 않는다. 자세한 기준은
[handler interface spec](../../spec/server/languages/java/02-handler-interfaces.ko.md)이 소유한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Getting Started](02-getting-started.ko.md) | [다음: Channel Messaging](04-channel-messaging.ko.md)
<!-- framework-adapter-nav:bottom:end -->
