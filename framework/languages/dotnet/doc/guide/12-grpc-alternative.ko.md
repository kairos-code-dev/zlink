<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: 인터페이스 카탈로그](./11-interface-catalog.ko.md) | [다음: 케이스 — 전자상거래 체크아웃](./13-case-ecommerce-checkout.ko.md)
<!-- framework-adapter-nav:end -->

# gRPC 대안으로 ZLink 선택하기 — 비슷한 서비스를 새로 만든다면

> 이 챕터는 **마이그레이션 가이드가 아니다.** 이미 gRPC(또는 내부 REST)로 잘 도는
> 시스템은 굳이 바꿀 이유가 없다. 이 문서는 **그와 비슷한 서버 간 통신 서비스를
> 새로 만들거나 크게 확장·재작성할 때**, 같은 결과를 ZLink Framework 로 얼마나 더
> 단순하게 얻는지 보여 주며 **도입을 권유**한다(그리고 **무엇은 여전히 gRPC 가
> 맞는지**도 솔직히 본다). 표면 매핑은
> [04-channel-messaging](./04-channel-messaging.ko.md) §0 와
> [11-interface-catalog](./11-interface-catalog.ko.md) §1.6 이, 사용법은 04 챕터가
> 소유한다. 도메인별 상세 사례는 §3 의 개별 케이스 문서(13~17)가 다룬다.

## 1. gRPC 는 혼자 끝나지 않는다

gRPC 자체는 빠르고 좋다. 문제는 이런 류의 서비스를 **"프로덕션급"** 으로 만들려면
공식 베스트프랙티스가 곧바로 추가 인프라를 요구한다는 점이다. 새로 설계한다면
아래를 처음부터 다 떠안는다는 뜻이다.

- **channel/stub 재사용 강제.** gRPC 공식 가이드의 첫 권고는 "Always re-use stubs
  and channels when possible" 다. 호출마다 channel 을 만들면 지연이 크게 늘기
  때문에, 보통 channel factory/pool 로 수명 관리를 직접 한다.
  ([grpc.io performance](https://grpc.io/docs/guides/performance/))
- **deadline 을 매 호출에 직접.** "단일 느린 RPC 가 상위 서비스를 무한정 막지
  않도록 모든 RPC 에 deadline 을 건다"가 표준 조언이다.
  ([Microsoft Learn](https://learn.microsoft.com/en-us/aspnet/core/grpc/performance))
- **로드밸런싱이 L4 로 안 된다.** gRPC 는 HTTP/2 의 **단일 long-lived 연결에 모든
  호출을 multiplex** 한다. 그래서 Kubernetes 의 기본 connection-level(L4)
  로드밸런싱은 연결 하나를 한 백엔드에 고정해 버려, 부하가 한쪽으로 쏠린다.
  해결하려면 **request-level(L7) 분배**가 필요하고, 이는 보통 (a) client-side LB +
  name resolver, (b) headless service + DNS, (c) **Envoy/Istio 같은 service mesh
  sidecar** 중 하나를 끌어온다.
  ([Kubernetes 블로그](https://kubernetes.io/blog/2018/11/07/grpc-load-balancing-on-kubernetes-without-tears/))
- **streaming 은 한번 시작하면 LB 가 안 된다.** 공식 가이드도 "streams cannot be
  load balanced once they have started" 라며 streaming 은 이득이 분명할 때만 쓰라고
  경고한다. ([grpc.io performance](https://grpc.io/docs/guides/performance/))
- **그 밖에** service discovery(Eureka/Consul/DNS/xDS), retry·hedging(service
  config), `.proto` 컴파일 파이프라인, mTLS, 그리고 **이벤트 fan-out 은 또 별도
  broker**(Kafka/NATS)로 간다.

대형 사례가 이를 잘 보여 준다. Netflix 는 1,000개 이상의 마이크로서비스를 운영하며
service discovery 를 **Eureka** 로, 호출 분배·관측을 **service mesh** 로 따로
얹는다(Eureka 자체가 단일 장애점이 될 수 있다는 점도 알려져 있다). Uber 역시 1,000개
넘는 서비스로 늘며 의존성 관리가 별도 과제가 됐다. 공통적으로 "통합 로그·메트릭·
트레이싱 없이는 분산 시스템을 디버깅할 수 없다"는 교훈이 반복된다.
([Netflix service mesh 사례](https://vivekbansal.substack.com/p/system-design-study-netflixs-adoption),
[scaling microservices](https://www.netguru.com/blog/scaling-microservices))

즉 "gRPC 를 쓴다"는 실제로는 **gRPC + L7 LB(보통 service mesh) + discovery +
event broker + proto 파이프라인**을 함께 운영한다는 뜻이다.

## 2. ZLink 가 한 겹으로 접는 지점

ZLink Framework 의 핵심은 호출 단위를 **논리 `channel name` 하나**로 좁히고,
위치 해결·연결·request-level 분배·correlation 을 framework 가 소유하는 것이다.
그래서 위 인프라 상당수가 **별도 컴포넌트 없이** framework 안으로 들어온다.

### 2.1 배치 구조 — 기존 스택 vs ZLink

같은 "두 서비스가 서로 호출 + 이벤트 + 외부 client" 토폴로지를 두 방식으로 그리면,
사라지는 박스가 곧 줄어드는 운영 부담이다.

```text
[기존]  gRPC + service mesh + event broker + WS gateway

   order-service (pod)                 payment-service (pod)
  ┌────────────────────┐             ┌────────────────────┐
  │ app + gRPC stub     │             │ app + gRPC server   │
  │        ↕            │             │        ↕            │
  │ Envoy sidecar  ─────┼─── mTLS ───▶│  Envoy sidecar      │
  └─────────┬──────────┘             └─────────┬──────────┘
            └──────────────┬──────────────────┘ xDS
                  ┌─────────▼───────────┐
                  │ mesh control plane   │ discovery + L7 LB + mTLS
                  │ (Istio / Envoy xDS)  │
                  └─────────────────────┘
   ┌──────────────────┐               ┌─────────────────────┐
   │ Kafka  (이벤트)   │               │ WS gateway           │
   │                  │               │ (외부 client 수용)    │
   └──────────────────┘               └─────────────────────┘
```

> mesh 를 안 쓰는 스택이라면 control plane 대신 별도 service discovery
> (Eureka/Consul/DNS)와 client-side LB 가 그 자리에 온다(§1). 어느 쪽이든 호출
> 분배·위치 해결은 **앱 밖의 별도 컴포넌트**다.

```text
[ZLink]  ZLink Framework + Registry 한 겹

   order-service                       payment-service
  ┌────────────────────┐             ┌────────────────────┐
  │ app                 │             │ app                 │
  │ ZLink Framework ────┼── channel ──▶  ZLink Framework    │
  │ (channel client)    │   name      │ (channel server)    │
  └─────────┬──────────┘             └─────────┬──────────┘
            └───────────────┬──────────────────┘
                     ┌───────▼───────┐
                     │   Registry    │  discovery + topology
                     └───────────────┘

  · 이벤트     → 같은 framework 의 fanout channel   (Kafka 불필요)
  · 외부 client → 같은 framework 의 STREAM           (별도 WS gateway 불필요)
  · L7 LB / sidecar / mesh control plane             (없음 — framework 가 흡수)
```

박스로 보면 **Envoy sidecar 2개 + mesh control plane(discovery·L7 LB·mTLS) +
Kafka + WS gateway** 가 빠지고, 그 책임이 framework 와 Registry 한 겹으로 들어온다.

### 2.2 한 번의 호출이 지나는 경로

cross-service 호출 한 번이 거치는 hop 도 줄어든다(시퀀스).

```mermaid
sequenceDiagram
  autonumber
  participant A as order-service
  participant SA as Envoy local
  participant SB as Envoy remote
  participant B as payment-service
  A->>SA: gRPC Charge
  SA->>SB: discovery + L7 LB 후 mTLS HTTP/2
  SB->>B: forward
  B-->>A: reply (sidecar 역경로)
```

```mermaid
sequenceDiagram
  autonumber
  participant A as order-service
  participant B as payment-service
  Note over A: channel 위치는 Registry view 가 이미 해결해 둠
  A->>B: Request(payments, Charge) — framework 가 peer 분배
  B-->>A: reply
```

### 2.3 접히는 항목 요약

| gRPC 베스트프랙티스/필요 인프라 | ZLink 에서 | 비고 |
|----------------------------------|------------|------|
| "stub/channel 을 재사용하라" | `IZLinkClient` 가 DI singleton, socket 수명은 framework 가 관리 | 호출마다 만들 일이 없음 |
| 모든 RPC 에 deadline | `Request(...).Timeout(...)` | reply 대기 시간 |
| L7 로드밸런싱(Envoy/Istio sidecar) | channel name + `Discovery` 로 framework 가 peer 분배 | sidecar 불필요 |
| service discovery(Eureka/xDS) | `UseDiscovery(...)` + Registry | [08-registry](./08-registry.ko.md) |
| interceptor | `IZLinkHandlerFilter` | [04](./04-channel-messaging.ko.md) §5 |
| 이벤트 broker(Kafka/NATS) | fanout channel pub/sub | 경계는 §4 참고 |
| 통합 관측(mesh telemetry) | runtime monitoring 이벤트 | [09-monitoring](./09-monitoring.ko.md) |
| 양방향 streaming | STREAM session | [07-stream](./07-stream.ko.md) |

> 한 줄 요약: **channel name + Registry 하나가 discovery + request-level 분배 +
> (상당 부분의) 이벤트 전파를 동시에 가져간다.** gRPC 스택에서 sidecar 와 별도
> discovery 컴포넌트가 빠지는 자리가 여기다.

## 3. 케이스 스터디 — 도메인별 개별 문서

도메인별로 "**기존 스택 → ZLink 구성 → 사라지는 인프라 → 경계**" 를 개별 문서로
정리했다. 각 문서는 실제 참조 아키텍처를 인용하며, 전자상거래는 풀 코드 워크스루,
나머지는 아키텍처 매핑 중심이다.

| 케이스 | 무엇을 보나 | ZLink 핵심 기능 |
|--------|-------------|-----------------|
| [전자상거래 체크아웃](./13-case-ecommerce-checkout.ko.md) | request/response·send·pub/sub 풀 코드 | channel messaging |
| [내부 마이크로서비스 mesh + 운영](./14-case-microservice-mesh.ko.md) | 다수 서비스 호출·BFF·topology 운영 | channel + Registry + monitoring |
| [실시간 멀티플레이 게임](./15-case-realtime-game.ko.md) | 영속 연결·방 상태·재접속 이전성 | STREAM + SPOT + actor + session dispatch |
| [라이드헤일링 디스패치](./16-case-ride-hailing.ko.md) | 대량 위치 fan-out·지역 매칭 | STREAM + pub/sub + zone SPOT |
| [채팅·메시징](./17-case-chat-messaging.ko.md) | room fan-out·presence·연결 관리 | STREAM + room SPOT + pub/sub |

> 등록 코드의 정식 사용법은 각 기능 챕터(04~09)가 소유한다. 케이스 문서는 "어떤
> 기능을 어떻게 조합하나"의 아키텍처 매핑을 보여 준다.

## 4. 솔직한 경계 — 여전히 gRPC/REST 가 맞는 곳

ZLink 가 모든 server-to-server 통신의 상위 호환은 아니다. 다음은 그대로 두는 게
낫다.

- **외부 공개 API.** 서드파티가 호출하는 공개 계약은 HTTP/gRPC/REST 가 표준이다.
  ZLink 는 **내부 server-to-server** 와 **외부 client(STREAM)** 에 강하다.
- **polyglot proto-first 계약.** `.proto` 를 단일 진실로 여러 언어 stub 을 찍어내는
  워크플로가 핵심이면 gRPC 가 낫다. ZLink 의 codec(protobuf/json/messagepack)은
  payload 직렬화이지 IDL-first 계약 생성 도구가 아니며, framework 표면은 `.NET`
  우선이다.
- **자동 retry/hedging.** framework 는 호출을 몰래 재시도하지 않는다.
  `ZLinkFrameworkException.IsRetriable` 은 분류 힌트일 뿐이고 retry 는 응용
  책임이다([06-actor-session](./06-actor-session.ko.md) §6).
- **broker 의 영속성/replay.** pub/sub 는 transport fan-out 이다. at-least-once
  영속 큐, consumer group offset, 장기 replay 가 필요하면 Kafka/NATS 가 맞다.
  `Submit(...)` 의 완료는 transport 위임까지만 보장한다([03-concepts](./03-concepts.ko.md) §7).
- **데이터 영속·조회.** ZLink 는 transport·dispatch 계층이지 datastore 가 아니다.
  game progression·메시지 이력·geo-index 같은 **영속/조회 상태는 DB·캐시(Redis 등)**
  가 맡는다. SPOT/actor 의 인메모리 상태는 그 lifetime 동안만 유지된다(게임·채팅
  케이스의 "DB 가 맡는다"가 이 줄을 가리킨다).
- **HTTP/2·grpc-web 그 자체.** 브라우저 grpc-web 호환이나 HTTP/2 인프라 자체가
  목적이면 ZLink 가 그 자리를 대신하지 않는다.

## 5. 언제 ZLink 를 고르나 — 도입 판단

이 문서는 교체를 강요하지 않는다. **이미 잘 도는 gRPC 시스템은 그대로 둔다.**
ZLink 는 **새 서비스/바운디드 컨텍스트를 시작**하거나 **큰 확장·재작성** 시점에
후보로 본다.

**적합 신호 (ZLink 가 잘 맞음)**

- `.NET` 백엔드에서 **내부 server-to-server** 통신이 중심이다.
- sidecar/service mesh(Envoy/Istio) 운영 부담을 처음부터 지고 싶지 않다.
- room/stage/zone 같은 **동적 노드**나 외부 game/mobile **client(STREAM)** 수용이
  로드맵에 있다(gRPC + 별도 WebSocket gateway 조합을 피하고 싶다).
- 서비스 위치·연결·재연결·correlation 을 framework 가 가져가길 원한다.

**회피 신호 (gRPC/REST 가 나음)** — 자세한 이유는 §4.

- 서드파티가 부르는 **외부 공개 API**, polyglot **proto-first** 계약,
  broker 의 **영속성/replay** 가 핵심 요건일 때.

**새 서비스라면 시작은 이렇게**

1. [02-getting-started](./02-getting-started.ko.md) 의 두-앱 예제로 channel 하나를
   띄워 동작을 확인한다.
2. request/response·send·pub/sub 를 [04-channel-messaging](./04-channel-messaging.ko.md)
   기준으로 channel 을 늘려 간다.
3. 동적 노드는 [05-spot](./05-spot.ko.md), 외부 client 는
   [07-stream](./07-stream.ko.md) 으로 확장한다.

기존 gRPC 시스템과 **공존**도 가능하다. 새 바운디드 컨텍스트만 ZLink 로 두고
기존 서비스는 그대로 둔 채, 필요할 때 한 경로씩 ZLink channel 로 노출하면 된다
(서로 다른 transport 라 프로세스/네트워크상 충돌하지 않는다).

## 6. 더 보기

- 케이스 스터디(개별): [13 전자상거래](./13-case-ecommerce-checkout.ko.md) ·
  [14 mesh+운영](./14-case-microservice-mesh.ko.md) ·
  [15 게임](./15-case-realtime-game.ko.md) ·
  [16 라이드헤일링](./16-case-ride-hailing.ko.md) ·
  [17 채팅](./17-case-chat-messaging.ko.md)
- 표면 매핑 한눈에: [04-channel-messaging](./04-channel-messaging.ko.md) §0,
  [11-interface-catalog](./11-interface-catalog.ko.md) §1.6
- 호출/handler 사용법: [04-channel-messaging](./04-channel-messaging.ko.md)
- discovery·Registry: [08-registry](./08-registry.ko.md)
- 외부 client(양방향 streaming) 흡수: [07-stream](./07-stream.ko.md)
- 기능 선택 지도: [10-feature-map](./10-feature-map.ko.md)

### 참고 자료

- gRPC Performance Best Practices — https://grpc.io/docs/guides/performance/
- Performance best practices with gRPC (.NET) — https://learn.microsoft.com/en-us/aspnet/core/grpc/performance
- gRPC Load Balancing on Kubernetes without Tears — https://kubernetes.io/blog/2018/11/07/grpc-load-balancing-on-kubernetes-without-tears/
- System Design Study: Netflix's adoption of Service Mesh — https://vivekbansal.substack.com/p/system-design-study-netflixs-adoption
- Scaling Microservices: Lessons from Netflix, Uber, Amazon, and Spotify — https://www.netguru.com/blog/scaling-microservices
- Metaplay — Game Server Architecture (actor entities, gateway/session) — https://docs.metaplay.io/game-server-programming/introduction-to-the-game-server-architecture.html
- AWS — Multiplayer Session-Based Game Hosting Guidance — https://aws.amazon.com/solutions/guidance/multiplayer-session-based-game-hosting-on-aws/
- Architecting an Uber-scale real-time tracking & dispatch system — https://dev.to/madhur_banger/architecting-an-uber-scale-real-time-tracking-dispatch-system-3a72
- Chat Application Architecture (GetStream) — https://getstream.io/blog/chat-application-architecture/
- Scaling Pub/Sub with WebSockets and Redis (Ably) — https://ably.com/blog/scaling-pub-sub-with-websockets-and-redis
