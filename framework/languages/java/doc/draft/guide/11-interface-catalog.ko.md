<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Monitoring](./10-monitoring.ko.md) | [다음: gRPC 대안](./12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- Java Interface Catalog Guide

> 이 문서는 **구현 전 초안**이다.
> 전체 계약은 [handler-interfaces](../handler-interfaces.ko.md)가 소유한다.

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
| session | `ZLinkSession` |
| actor push | `ZLinkBoundSession` |
| connector | `ZLinkStreamConnector` |
| monitoring | `ZLinkRuntimeEventHandler<TEvent>` |

## 2. Kotlin wrapper

Kotlin wrapper는 Java API 위의 thin wrapper다. Java와 다른 lifecycle, buffering,
error 의미를 만들지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: Monitoring](./10-monitoring.ko.md) | [다음: gRPC 대안](./12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:bottom:end -->
