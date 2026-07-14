<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Feature Map](10-feature-map.ko.md) | [다음: gRPC 대안](12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin Interface Catalog Guide

## 1. 가장 자주 쓰는 타입

inbound handler는 Kotlin coroutine interface(`ZLinkSuspending*`)를, outbound·관리
표면은 Java contract를 그대로 쓴다(suspend 확장으로 호출).

| 용도 | 타입 |
|------|------|
| channel request/send | `ZLinkClient` (+ `request<R>`/`send` 확장) |
| fanout publish | `ZLinkFanoutClient` (+ `publishToTopic` 확장) |
| routed channel | `ZLinkRouteClient` (+ `request<R>`/`send` 확장) |
| request handler | `ZLinkSuspendingRequestHandler<TRequest, TReply>` |
| send handler | `ZLinkSuspendingSendHandler<TMessage>` |
| publish handler | `ZLinkSuspendingPublishHandler<TEvent>` |
| Spot 베이스 | `ZLinkSuspendingSpot<TActor>` |
| Entry Spot 베이스 | `ZLinkSuspendingEntrySpot<TActor>` |
| Spot actor handler | `ZLinkSuspendingSpotActorRequestHandler` / `...SendHandler` |
| Spot timer | `ZLinkSuspendingSpotTimerHandler<TSpot>` |
| actor factory | `ZLinkSuspendingActorFactory` |
| actor remote state 이동 | `ZLinkSuspendingActorTransferAdapter<TActor>` |
| Spot 관리 | `ZLinkSpotManager` |
| actor 관리 | `ZLinkActorManager` |
| session | `ZLinkSuspendingSession` |
| session packet handler | `ZLinkSuspendingTypedSessionPacketHandler<TSessionContext : ZLinkSessionContext, TMessage : Any>` |
| actor push | `ZLinkBoundSession` |
| connector | `ZLinkStreamConnector` → `connector.kotlin()` |
| monitoring | `ZLinkRuntimeEventHandler<TEvent>` (동기 관찰) |

## 2. Java 베이스와의 관계

Kotlin 표면은 Java framework 위의 thin coroutine 레이어다. Java와 다른 lifecycle,
buffering, error 의미를 **만들지 않는다.** `ZLinkSuspending*` 베이스/interface는
내부에서 Java 콜백을 coroutine으로 잇기만 하고, 등록 표면(`addClientServerChannel`,
`addSpotMesh`, `addStreamNode` 등)과 관리 bean(`ZLinkClient`, `ZLinkSpotManager`,
spec [handler-interfaces](../../spec/server/languages/java/02-handler-interfaces.ko.md)를 본다.

Java 표면 ↔ Kotlin 표면 1:1 대응표는
[kotlin README §0](../README.ko.md#0-kotlin-표면-한눈에)에 있다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Feature Map](10-feature-map.ko.md) | [다음: gRPC 대안](12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:bottom:end -->
