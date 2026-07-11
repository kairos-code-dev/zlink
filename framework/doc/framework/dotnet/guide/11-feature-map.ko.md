<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Monitoring](10-monitoring.ko.md) | [다음: 인터페이스 카탈로그](12-interface-catalog.ko.md)
<!-- framework-adapter-nav:end -->

# 11. 기능 맵 — 무엇을, 얼마나 쉽게, 언제

> 각 기능의 사용법은 앞 챕터(04~09)가, 정식 계약은 spec 문서가 다룬다. 이 문서는
> 기능 선택을 돕는 지도다.

## 1. 난이도 기준

| 등급 | 의미 |
|------|------|
| **낮음** | handler 1개 + channel 등록 수준. 가이드만으로 도입 가능 |
| **중간** | lifecycle/factory/등록 조합을 이해해야 함 |
| **높음** | 분산 토폴로지·session routing 결정이 필요 |

## 2. 기능 × 난이도 × 언제 쓰나

| 기능 | 난이도 | 언제 쓰나 | 가이드 | 정식 문서 |
|------|:------:|-----------|--------|-----------|
| 서버 간 request/response | 낮음 | 서비스 A가 서비스 B의 결과가 필요할 때 | [4](04-channel-messaging.ko.md) | [channel-messaging](../../common/spec/languages/dotnet/aspnet-core-channel-messaging.ko.md) |
| 서버 간 단방향 send | 낮음 | 응답 없는 작업 위임/통지 | [4](04-channel-messaging.ko.md) | [channel-messaging](../../common/spec/languages/dotnet/aspnet-core-channel-messaging.ko.md) |
| pub/sub 이벤트 fan-out | 낮음 | domain event 를 여러 구독자에게 전파 | [4](04-channel-messaging.ko.md) | [channel-messaging](../../common/spec/languages/dotnet/aspnet-core-channel-messaging.ko.md) |
| SPOT(room/stage/zone) | 중간 | 동적 생성·소멸 논리 단위 라우팅 | [5](05-spot.ko.md) | [spot](../../common/spec/languages/dotnet/aspnet-core-spot.ko.md) |
| 일반 handler 에서 Spot 흐름 진입 | 중간 | HTTP/세션 gateway 가 actor 생성 또는 Entry Spot join 으로 `ActorRef` 확보 | [5](05-spot.ko.md) | [spot](../../common/spec/languages/dotnet/aspnet-core-spot.ko.md) |
| Spot yield dispatch | 높음 | player admission처럼 공용 상태 의존 없는 I/O 대기 중 다른 actor/timer를 막지 않을 때 | [5](05-spot.ko.md) | [async-execution-policy](../../common/spec/async-execution-policy.ko.md) |
| Spot timer (게임 루프 등) | 중간 | 주기 tick, heartbeat, 정리 작업 | [5](05-spot.ko.md) | [spot](../../common/spec/languages/dotnet/aspnet-core-spot.ko.md) |
| Stage wrapper | 중간 | `playhouse` Stage 류를 SPOT 위에 얹을 때 | [5](05-spot.ko.md) | [stage-wrapper](../../common/spec/languages/dotnet/stage-wrapper-on-spot.ko.md) |
| actor / Entry Spot | 높음 | session 과 묶인 actor 로 packet 자동 dispatch | [6](06-actor-spot.ko.md) | [actor](../../common/spec/languages/dotnet/aspnet-core-actor.ko.md) |
| session actor dispatch | 높음 | 연결 서버와 로직 서버를 분리(재접속 이전성) | [6](06-actor-spot.ko.md) | [session-actor-dispatch](../../common/spec/languages/dotnet/session-actor-dispatch.ko.md) |
| STREAM session(서버) | 중간 | 외부 client(TCP/WS)를 framework 로 받기 | [8](08-stream.ko.md) | [stream](../../common/spec/languages/dotnet/aspnet-core-stream.ko.md) |
| Stream Connector(client) | 중간 | client 측에서 STREAM 서버에 접속 | [8](08-stream.ko.md) | [streaming-client](samples/streaming-client.ko.md) |
| Location 자동 연결·운영 조회 | 중간 | endpoint 를 코드에 적지 않고 서버 증감을 따라가고 싶을 때 | [9](09-location.ko.md) | [location runtime](../../common/spec/location-runtime.ko.md) |
| spot 주소 메시징 | 중간 | 다른 노드의 spot/actor 로 반복해서 보낼 때(조회 1회 후 주소 보관) | [5](05-spot.ko.md) §5 | [spot 주소 메시징](../../common/spec/spot-address-messaging.ko.md) |
| runtime monitoring | 낮음 | socket/location/spot 이벤트 관찰 | [10](10-monitoring.ko.md) | [monitoring](../../common/spec/languages/dotnet/aspnet-core-monitoring.ko.md) |

## 3. 빠른 선택 가이드

```mermaid
%%{init: {'themeVariables': {'edgeLabelBackground':'transparent'}}}%%
flowchart TD
  Q1{외부 client를<br/>직접 받나?} -->|예| Stream[STREAM + Connector<br/>07]
  Q1 -->|아니오| Q2{동적 단위<br/>room/stage가 있나?}
  Q2 -->|아니오| Q3{응답이<br/>필요한가?}
  Q3 -->|예| Req[request/response<br/>04]
  Q3 -->|아니오| Q4{여러 구독자에게<br/>흩뿌리나?}
  Q4 -->|예| Pub[pub/sub<br/>04]
  Q4 -->|아니오| Send[one-way send<br/>04]
  Q2 -->|예| Q5{참가자별 상태/<br/>세션이 있나?}
  Q5 -->|아니오| Spot[SPOT<br/>05]
  Q5 -->|예| Q6{연결 서버와<br/>로직 서버를 나누나?}
  Q6 -->|아니오| Actor[actor + SPOT<br/>06]
  Q6 -->|예| Sad[session actor dispatch<br/>06]
```

- **서비스끼리 호출만** → request/response 또는 send. 난이도 낮음,
  [02-getting-started](02-getting-started.ko.md)로 충분.
- **이벤트를 흩뿌린다** → pub/sub.
- **방/판/존 같은 동적 단위** → SPOT. 그 안에 참가자별 상태/세션이 있으면 actor.
- **일반 handler 에서 Spot 흐름으로** → actor 생성 또는 Entry Spot join 으로 `ActorRef` 확보.
- **외부 게임/모바일 client** → STREAM(서버) + Stream Connector(client).
- **연결 서버와 로직 서버 분리(재접속 이전성)** → session actor dispatch.

## 4. 기능을 실제로 보여 주는 샘플

각 sample은 의도가 겹치지 않게 서로 다른 기능 묶음을 맡는다. "이 기능을 실행 코드로
보고 싶다"면 아래에서 대표 sample로 이동한다. 언어 중립 업무 흐름은 공통 sample이,
`.NET` 등록과 실행 방법은 연결된 문서가 맡는다.

| sample | 핵심 기능 묶음 | codec | 언어별 실행 문서 |
|--------|----------------|:-----:|------------------|
| TicTacToe | 수동 endpoint 직접 연결, STREAM auth, actor game join | JSON | [TicTacToe](samples/tictactoe-game-sample.ko.md) |
| Bingo | location store 자동 연결, 분리 gateway, Entry Spot, room Spot timer, bound push | Protobuf | [Bingo](samples/bingo-game-sample.ko.md) |
| SupportChat | conversation Spot, reconnect 이전성, idle timer→close, bound push | JSON | [SupportChat](samples/supportchat-sample.ko.md) |
| DeliveryDispatch | HTTP intake, timeout 재배정, status fanout, delivery Spot, 고객 push | JSON | [DeliveryDispatch](samples/deliverydispatch-sample.ko.md) |
| ShoppingMall | event sourcing, OrderId owner routing, projection rebuild, 보상, scale-out | JSON | [ShoppingMall](samples/shoppingmall-sample.ko.md) |
| GameQuest | fanout 구독, player owner, quest event sourcing, reward idempotency | JSON | [GameQuest](samples/gamequest-sample.ko.md) |

> 기능별 사용법은 04~09 가, 샘플의 언어 중립 공통 시나리오는
> [spec/sample](../../common/sample/README.ko.md) 가 다룬다.

## 5. 이동 중 actor request의 현재 제약

현재 framework가 고정한 `Systems.Zlink 8.6.4`를 사용하면 actor가 다른 node로 이동하는
동안 보낸 request의 응답이 원래 caller에 연결되지 않고 timeout될 수 있다. 이동 중
send와 request frame 보존, target handler 실행은 지원하지만 request/reply까지 완료된다고
간주하면 안 된다. 이동한 actor의 응답 연결을 지원하는 bindings 패키지로 중앙 버전을
갱신한 뒤 해당 시나리오를 사용해야 한다. framework에서 source node를 다시 거치게 만드는
우회는 target이 caller에 직접 응답해야 하는 공통 계약과 다르므로 제공하지 않는다.

## 6. 더 보기

- 핵심 개념: [03-concepts](03-concepts.ko.md)
- ZLink 을 어디에 쓰나(새 서비스 도입 판단): [13-grpc-alternative](13-grpc-alternative.ko.md)
- 모든 계약 인터페이스를 코드로(ContractTests 검증): [12-interface-catalog](12-interface-catalog.ko.md)
- 전체 인터페이스 카탈로그(언어 중립 정식): [spec/handler-interfaces](../../common/spec/languages/dotnet/handler-interfaces.ko.md)
- 동작 계약은 각 기능 spec을 따르고, 검증 범위는
  [regression test matrix](../internals/regression-test-matrix.ko.md)에서 확인한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Monitoring](10-monitoring.ko.md) | [다음: 인터페이스 카탈로그](12-interface-catalog.ko.md)
<!-- framework-adapter-nav:bottom:end -->
