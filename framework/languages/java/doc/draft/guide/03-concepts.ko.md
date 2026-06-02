<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Getting Started](./02-getting-started.ko.md) | [다음: Feature Map](./04-feature-map.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- Java Concepts

> 이 문서는 **구현 전 초안**이다.
> Java/Spring Boot 사용자가 알아야 할 핵심 개념만 설명한다.

## 1. Channel

channel은 논리 호출 이름이다. application은 endpoint 대신 channel name을 기준으로
request, send, publish를 호출한다.

| Channel 종류 | 용도 |
|--------------|------|
| client/server | 일반 request/send |
| fanout | pub/sub event |
| dealer mesh | dealer끼리 직접 연결되는 mesh client |
| route mesh | target `RoutingId`를 지정하는 routed channel |

## 2. Capability

capability는 한 channel이나 SpotNode가 수행하는 역할이다. 같은 capability 안에서
discovery와 manual connection을 섞지 않는다.

예:

- `price.server`
- `price.client`
- `event.publisher`
- `event.subscriber`
- `play.router`
- `play.pubsub`

## 3. Handler 노출

handler scan은 후보를 찾는 단계다. 실제 노출은 channel 등록에서 정한다. framework는
scan된 handler를 모든 channel에 자동으로 열지 않는다.

## 4. Spot

Spot은 room, stage, zone처럼 동적으로 생성되는 논리 노드다. Java 포팅은 Spot type
factory, Entry Spot, timer, actor dispatch를 `.NET`과 같은 의미로 제공한다.

## 5. STREAM과 Connector

server 쪽은 `ZLinkSession` 하나를 기준으로 header 기반 packet을 받는다. 외부 client는
server framework에 의존하지 않는 `ZLinkStreamConnector` 모듈을 사용한다.

## 6. Spring DI

Spring bean 주입은 capability 가능성을 암시해야 한다. SpotNode가 없으면
`ZLinkSpotManager`를 등록하지 않고, actor factory가 없으면 `ZLinkActorManager`를
등록하지 않는다. 자세한 기준은
[DI capability policy](../internals/di-capability-exposure-policy.ko.md)가 소유한다.
