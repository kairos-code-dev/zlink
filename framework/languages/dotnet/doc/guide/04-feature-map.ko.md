<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: 핵심 개념 — .NET 표면 멘탈 모델](./03-concepts.ko.md) | [다음: ZLink Framework .NET Interface Catalog](../spec/handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

# 기능 맵 — 무엇을, 얼마나 쉽게, 언제

> 각 기능의 정식 계약은 아래 표의 "정식 문서"가 소유한다. 이 문서는 기능
> 선택을 돕기 위한 지도일 뿐, 새 의미를 정의하지 않는다.

## 1. 난이도 기준

| 등급 | 의미 |
|------|------|
| **낮음** | handler 1개 + channel 등록 1줄 수준. 가이드만으로 도입 가능. |
| **중간** | lifecycle/factory/등록 조합을 이해해야 함. spec 문서 동반 필요. |
| **높음** | 분산 토폴로지·세션 라우팅 결정이 필요. spec + 샘플 동반 필요. |

## 2. 기능 × 난이도 × 언제 쓰나

| 기능 | 난이도 | 언제 쓰나 | 정식 문서 | 샘플 |
|------|:------:|-----------|-----------|------|
| 서버 간 request/response | 낮음 | 서비스 A가 서비스 B의 결과가 필요할 때 | [channel-messaging](../spec/aspnet-core-channel-messaging.ko.md) | [channel 샘플](./samples/channel-messaging-samples.ko.md) |
| 서버 간 단방향 send | 낮음 | 응답이 필요 없는 작업 위임/통지 | [channel-messaging](../spec/aspnet-core-channel-messaging.ko.md) | [channel 샘플](./samples/channel-messaging-samples.ko.md) |
| pub/sub 이벤트 fan-out | 낮음 | domain event를 여러 구독자에게 전파 | [channel-messaging](../spec/aspnet-core-channel-messaging.ko.md) | [channel 샘플](./samples/channel-messaging-samples.ko.md) |
| SPOT(room/stage/zone) | 중간 | 동적 생성·소멸되는 논리 노드 단위 라우팅 | [spot](../spec/aspnet-core-spot.ko.md) | [SPOT 샘플](./samples/spot-samples.ko.md) |
| Stage wrapper | 중간 | `playhouse` Stage류를 SPOT 위에 얹을 때 | [stage-wrapper-on-spot](../spec/stage-wrapper-on-spot.ko.md) | [SPOT 샘플](./samples/spot-samples.ko.md) |
| actor / Entry Spot | 높음 | session과 묶인 actor로 packet 자동 dispatch | [actor](../spec/aspnet-core-actor.ko.md) | [tictactoe](./samples/tictactoe-game-sample.ko.md) |
| session actor dispatch | 높음 | client 연결 서버와 actor 서버를 분리 | [session-actor-dispatch](../spec/session-actor-dispatch.ko.md) | [tictactoe](./samples/tictactoe-game-sample.ko.md) |
| STREAM session | 중간 | 외부 client(TCP/WS)를 framework로 받기 | [stream](../spec/aspnet-core-stream.ko.md) | [STREAM 샘플](./samples/stream-samples.ko.md) |
| Stream Connector | 중간 | client 측에서 STREAM 서버에 접속 | [stream](../spec/aspnet-core-stream.ko.md) | [connector 샘플](./samples/streaming-client.ko.md) |
| runtime monitoring | 낮음 | socket/discovery/registry/spot 이벤트 관찰 | [monitoring](../spec/aspnet-core-monitoring.ko.md) | — |
| registry topology 조회 | 중간 | 클러스터 topology snapshot/query | [registry](../spec/aspnet-core-registry.ko.md) | — |

## 3. 빠른 선택 가이드

- **서비스끼리 호출만 하면 된다** → request/response 또는 send. 난이도 낮음,
  [02-getting-started](./02-getting-started.ko.md)로 충분.
- **이벤트를 흩뿌린다** → pub/sub.
- **방/판/존 같은 동적 단위가 있다** → SPOT. 그 안에 참가자별 상태/세션이
  있으면 actor까지.
- **외부 게임/모바일 client를 직접 받는다** → STREAM(서버) + Stream
  Connector(client).
- **연결 서버와 로직 서버를 나눠야 한다(재접속 이전성)** → session actor
  dispatch.

## 4. 더 보기

- 표면 멘탈 모델: [03-concepts](./03-concepts.ko.md)
- 전체 인터페이스 카탈로그: [spec/handler-interfaces](../spec/handler-interfaces.ko.md)
- 동작/검증 기준: [internals/behavior-matrix](../internals/behavior-matrix.ko.md)
