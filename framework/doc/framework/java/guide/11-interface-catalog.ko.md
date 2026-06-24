<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Feature Map](10-feature-map.ko.md) | [다음: gRPC 대안](12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# Java Interface Catalog Guide

## 1. 가장 자주 쓰는 타입

| 용도 | 타입 |
|------|------|
| channel request/send | `ZLinkClient` |
| fanout publish | `ZLinkFanoutClient` |
| routed channel | `ZLinkRouteClient` |
| request handler | `ZLinkRequestHandler<TRequest, TReply>` |
| send handler | `ZLinkSendHandler<TMessage>` |
| publish handler | `ZLinkPublishHandler<TEvent>` |
| Spot 관리 | `ZLinkSpotManager` |
| actor 관리 | `ZLinkActorManager` |
| actor ref join | `ZLinkActorGateway` |
| session | `ZLinkSession` |
| actor push | `ZLinkBoundSession` |
| connector | `ZLinkStreamConnector` |
| monitoring | `ZLinkRuntimeEventHandler<TEvent>` |

## 2. Kotlin wrapper

Kotlin wrapper는 Java runtime 위에 coroutine/Flow 편의 표면을 추가한다 — suspending
handler 인터페이스, coroutine invoker, `callbackFlow` 기반 Flow adapter 를 제공한다.
buffering/lifecycle/error 의미는 각 Kotlin API별로 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Feature Map](10-feature-map.ko.md) | [다음: gRPC 대안](12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:bottom:end -->
