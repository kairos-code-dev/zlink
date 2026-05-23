<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: 인터페이스 카탈로그](./11-interface-catalog.ko.md) | [다음: ZLink Framework .NET Interface Catalog (spec)](../spec/handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

# gRPC·서버간 웹콜에서 옮겨오기

> 이 챕터는 사용법이 아니라 **도입 판단 문서**다. 이미 gRPC(또는 내부 REST)로
> 서버 간 통신을 하는 백엔드가, ZLink Framework 로 옮기면 무엇이 간단해지는지와
> **무엇은 여전히 gRPC 가 맞는지**를 함께 본다. 표면 매핑은
> [04-channel-messaging](./04-channel-messaging.ko.md) §0 와
> [11-interface-catalog](./11-interface-catalog.ko.md) §1.6 이, 사용법은 04 챕터가
> 소유한다.

## 1. gRPC 는 혼자 끝나지 않는다

gRPC 자체는 빠르고 좋다. 문제는 **"프로덕션급 gRPC"** 가 되려면 공식
베스트프랙티스가 곧바로 추가 인프라를 요구한다는 점이다.

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

| gRPC 베스트프랙티스/필요 인프라 | ZLink 에서 | 비고 |
|----------------------------------|------------|------|
| "stub/channel 을 재사용하라" | `IZLinkClient` 가 DI singleton, socket 수명은 framework 가 관리 | 호출마다 만들 일이 없음 |
| 모든 RPC 에 deadline | `Request(...).Timeout(...)` | reply 대기 시간 |
| L7 로드밸런싱(Envoy/Istio sidecar) | channel name + `Discovery` 로 framework 가 peer 분배 | sidecar 불필요 |
| service discovery(Eureka/xDS) | `UseDiscovery(...)` + Registry | [08-registry](./08-registry.ko.md) |
| interceptor | `IZLinkHandlerFilter` | [04](./04-channel-messaging.ko.md) §5 |
| 이벤트 broker(Kafka/NATS) | fanout channel pub/sub | 경계는 §5 참고 |
| 통합 관측(mesh telemetry) | runtime monitoring 이벤트 | [09-monitoring](./09-monitoring.ko.md) |
| 양방향 streaming | STREAM session | [07-stream](./07-stream.ko.md) |

> 한 줄 요약: **channel name + Registry 하나가 discovery + request-level 분배 +
> (상당 부분의) 이벤트 전파를 동시에 가져간다.** gRPC 스택에서 sidecar 와 별도
> discovery 컴포넌트가 빠지는 자리가 여기다.

## 3. 케이스 스터디 — 대형 서비스의 server-to-server 패턴

### 3.1 마이크로서비스 mesh (Netflix 류 다수 서비스)

| | 기존 스택 | ZLink |
|---|-----------|-------|
| 호출 | gRPC unary + stub | `IZLinkClient.Request(channel, req)` |
| 위치 해결 | Eureka/Consul | `UseDiscovery(...)` + Registry |
| 부하 분배 | Envoy/Istio sidecar(L7) | channel `Discovery` 가 peer 집합 분배 |
| 관측 | mesh telemetry + 별도 수집 | `AddZLinkMonitoring(...)` runtime 이벤트 |

서비스 수가 늘어도 응용 코드는 `Request("pricing", ...)` 처럼 **channel 이름만**
안다. 어디에 몇 개 떠 있는지는 Registry view 가 숨긴다.

### 3.2 내부 REST/gRPC fan-out

요청 하나가 여러 내부 서비스를 부르는 BFF/aggregator 패턴이다. 기존에는 서비스마다
base URL/stub 을 들고 HTTP client 또는 gRPC stub 을 따로 관리한다. ZLink 에서는
`IZLinkClient` 하나로 channel 이름만 바꿔 호출한다(응답 필요하면 `Request`, 통지면
`Send`).

### 3.3 이벤트 파이프라인 (Kafka 류)

domain event 를 여러 구독자에게 흘리는 경로다. ZLink 의 fanout channel pub/sub 로
**broker 없이** 같은 토폴로지를 만들 수 있다. 단 영속성/replay/consumer group
의미가 필요하면 broker 가 여전히 맞다(§5).

## 4. 플래그십 워크스루 — 전자상거래 체크아웃

현실적인 한 흐름을 끝까지 옮겨 본다. HTTP API gateway 가 `order-service` 를 부르고,
`order-service` 가 `payments`·`inventory` 를 호출한 뒤 `order.events` 로 상태를
흘린다. 주문 추적은 외부 client 로의 실시간 push(STREAM)다.

```mermaid
flowchart LR
  GW["API gateway<br/>(HTTP in)"] -->|"Request(\"orders\", PlaceOrder)"| ORD[order-service]
  ORD -->|"Request(\"payments\", Charge)"| PAY[payment-service]
  ORD -->|"Send(\"inventory\", ReserveStock)"| INV[inventory-service]
  ORD -->|"Publish(\"order.events\", ...)"| EV(("order.events"))
  EV --> NOTI[notification-service]
  EV --> ANALYTICS[analytics-service]
```

**기존 스택에서 이 그림에 필요한 것:** 각 서비스의 gRPC stub, Envoy/Istio mesh(L7
LB), service discovery, 이벤트용 Kafka, 그리고 주문추적용 별도 WebSocket gateway.

**ZLink 에서 사라지는 것:** sidecar mesh, 별도 discovery 컴포넌트, (이벤트가
broker 영속성을 요구하지 않는다면) Kafka, 별도 WS gateway(STREAM 으로 흡수).

### order-service 등록

```csharp
var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    // 들어오는 주문 요청을 받는 서버 channel
    options.AddClientServerChannel("orders", channel =>
    {
        channel.EnableServer(server => server.Bind("tcp://0.0.0.0:7401"));
        channel.AddRequestHandler<PlaceOrderHandler>();
    });

    // 나가는 호출용 client channel (gRPC stub 자리)
    options.AddClientServerChannel("payments", channel => channel.EnableClient());
    options.AddClientServerChannel("inventory", channel => channel.EnableClient());

    // 이벤트 fan-out (Kafka producer 자리)
    options.AddFanoutChannel("order.events", channel =>
        channel.EnablePublisher(publisher => publisher.Bind("tcp://0.0.0.0:7402")));

    // discovery 하나로 위 모든 channel 의 위치 해결 (Eureka/xDS + L7 LB 자리)
    options.UseDiscovery(discovery => discovery.Add("tcp://registry1:5551"));
    options.AddHandlersFromAssemblyOf<Program>();
});

builder.Build().Run();
```

### order-service handler

```csharp
[ZLinkRequest]
public sealed class PlaceOrderHandler(
    IZLinkClient services,
    IZLinkFanoutPublisher events)
    : IZLinkRequestHandler<PlaceOrder, OrderPlaced>
{
    public async ValueTask<OrderPlaced> HandleAsync(
        PlaceOrder request, ZLinkRequestContext context, CancellationToken ct)
    {
        // gRPC unary RPC -> request/response. deadline 은 Timeout 으로.
        var charge = await services
            .Request("payments", new Charge(request.AccountId, request.AmountMinor))
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync<Charged>(ct);

        // 응답이 필요 없는 명령 -> one-way send
        await services
            .Send("inventory", new ReserveStock(request.OrderId, request.Sku, request.Quantity))
            .Submit(ct);

        // server-streaming/이벤트 피드 -> pub/sub fan-out
        await events
            .Publish("order.events", "order.status",
                new OrderStatusChanged(request.OrderId, "Placed"))
            .Submit(ct);

        return new OrderPlaced(request.OrderId, charge.ReceiptId);
    }
}
```

### gateway 와 구독자 측

```csharp
// API gateway: client capability 만 열고 HTTP -> ZLink 로 중계
app.MapPost("/orders", async (PlaceOrderHttp body, IZLinkClient client, CancellationToken ct) =>
{
    var placed = await client
        .Request("orders", new PlaceOrder(body.OrderId, body.AccountId, body.AmountMinor, body.Sku, body.Quantity))
        .Timeout(TimeSpan.FromSeconds(3))
        .SubmitAsync<OrderPlaced>(ct);
    return Results.Ok(placed);
});

// notification-service: 이벤트 구독 (Kafka consumer 자리)
[ZLinkHandlerGroup("order.events")]
public sealed class OrderStatusNotifier : IZLinkPublishHandler<OrderStatusChanged>
{
    public ValueTask HandleAsync(
        OrderStatusChanged message, ZLinkPublishContext context, CancellationToken ct)
        => /* 푸시/이메일 발송 */ ValueTask.CompletedTask;
}
```

주문 추적을 외부 client(모바일/웹)로 실시간 push 하는 부분은 STREAM 으로
흡수한다([07-stream](./07-stream.ko.md)). 별도 WebSocket gateway 를 두지 않고
framework session 이 연결 수명·재연결·packet framing 을 가져간다.

## 5. 솔직한 경계 — 여전히 gRPC/REST 가 맞는 곳

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
- **HTTP/2·grpc-web 그 자체.** 브라우저 grpc-web 호환이나 HTTP/2 인프라 자체가
  목적이면 ZLink 가 그 자리를 대신하지 않는다.

## 6. 점진적 마이그레이션 순서

한 번에 갈아엎지 않는다. strangler 패턴으로 channel 단위로 옮긴다.

1. **outbound 부터.** 호출하는 쪽에 client capability(`EnableClient()`)와
   `UseDiscovery(...)` 만 추가해, 기존 gRPC 서버와 병행하며 한 호출 경로를 ZLink
   channel 로 바꾼다.
2. **서버 한 RPC 씩.** 받는 서비스에서 gRPC method 하나를
   `IZLinkRequestHandler<TReq, TRep>` 로 옮기고 `EnableServer(...)` + `Bind(...)`
   로 노출한다. packet 이름이 겹치지 않으면 같은 프로세스에서 gRPC 와 공존한다.
3. **이벤트 경로.** 통지성 이벤트(영속성 불필요)부터 fanout channel 로 옮기고,
   영속성이 필요한 스트림은 broker 에 남긴다.
4. **관측 연결.** `AddZLinkMonitoring(...)` 으로 socket/registry 이벤트를 기존
   로깅/메트릭에 합류시킨다.

각 단계는 [02-getting-started](./02-getting-started.ko.md) 의 두-앱 예제로 검증한
뒤 확장하면 된다.

## 7. 더 보기

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
