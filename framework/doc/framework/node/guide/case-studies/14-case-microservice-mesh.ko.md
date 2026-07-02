[문서 목록](../../README.ko.md) | [이전: 케이스 — 전자상거래 체크아웃](13-case-ecommerce-checkout.ko.md) | [다음: 케이스 — 실시간 멀티플레이 게임](15-case-realtime-game.ko.md)

# 케이스 — 내부 마이크로서비스 mesh + 운영

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> 다수 내부 서비스의 호출(BFF aggregation 포함)과 운영(topology·관측)을 다룬다.
> 실행 가능한 샘플이 아니라, ZLink 를 service mesh 대체/보완 위치에 둘지 판단하는
> 아키텍처 매핑 문서다.

> **이 케이스에서 ZLink 이 좋은 지점**
> - 다수 서비스 호출·BFF fan-out 을 channel name  + Registry 로 묶어 sidecar·별도 discovery 를 줄인다.
> - **그대로 남는 것**: retry·circuit-breaking·correlation 추적·외부 공개 API.
> - 즉 ZLink 은 호출 배선·위치 해결을 줄이고, 복원력 정책은 그대로 앱이 진다.

## 1. 도메인 — "서비스가 늘면 호출이 아니라 운영이 문제"

수십~수백 개 서비스가 서로 부르는 환경의 진짜 난제는 호출 자체가 아니다.

- **BFF fan-out 의 부분 실패와 지연 예산.** 한 화면 요청이 profile·pricing·
  inventory 등 N 개를 부른다. **가장 느린 호출이 전체 응답을 지배**하고, 한
  서비스가 죽으면 부분 응답·degrade 정책이 필요하다. 호출마다 deadline·retry·
  circuit-breaking 을 건다.
- **gRPC 의 L7 분배.** HTTP/2 long-lived 연결은 connection-level(L4) LB 로
  쏠린다 → Envoy/Istio 또는 client-side LB 가 필요하다([Kubernetes 블로그](https://kubernetes.io/blog/2018/11/07/grpc-load-balancing-on-kubernetes-without-tears/)).
- **"서비스는 다 healthy 한데 시스템이 실패"** — 각 서비스는 정상으로 보이는데
  전체 흐름이 깨지는 상황. 분산 추적(어느 호출이 어디서 느려졌나)·통합 메트릭
  없이는 디버깅이 안 된다. **correlation id**(한 요청에 같은 식별자를 붙여 서비스를
  건너가며 추적) 전파와 topology 가시성이 필수다.
  ([scaling microservices](https://www.netguru.com/blog/scaling-microservices))
- **서비스가 늘 때마다 같은 배선이 반복된다.** 새 서비스를 부를 때마다 `.proto`
  컴파일, stub 생성, channel factory, deadline 설정, discovery 등록, mTLS 구성이
  복붙된다. **서비스 수에 비례해 배선 코드와 mesh 설정이 늘어난다.**

이 중 **retry 정책·circuit breaking·correlation 전파·부분 실패 degrade(일부 실패해도
나머지로 응답) 는 도메인/정책 문제로 그대로 남는다.** ZLink 가 줄이는 건 위치
해결·L7 분배·sidecar 운영·서비스마다 반복되는 배선이다.

## 2. 기존 스택 — gRPC stub + mesh + discovery

### 2.1 컴포넌트와 그 이유

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| `.proto` + 코드 생성 | 서비스마다 계약을 stub 으로 찍어냄. CI 에 proto 컴파일 단계 |
| gRPC stub/channel | 호출. channel 재사용·deadline 을 직접 관리 |
| cockatiel/opossum(또는 유사) | **retry**(재시도)·**circuit-breaker**(연속 실패 시 잠시 차단)·timeout 정책 |
| Envoy sidecar + mesh control plane | HTTP/2 의 **L7(요청 단위) 분배**·mTLS·재시도 |
| service discovery(Consul/xDS) | 어느 pod 이 떠 있는지 추적 |
| OpenTelemetry collector + tracing 백엔드 | correlation 전파·분산 추적 수집 |

### 2.2 계약과 서버 (gRPC)

```proto
// pricing.proto — 서비스마다 한 벌, CI 에서 stub 으로 컴파일된다
syntax = "proto3";
service Pricing { rpc Quote (QuoteRequest) returns (QuoteReply); }
message QuoteRequest { string user_id = 1; }
```

```ts
@Injectable()
export class PricingService {
  constructor(private readonly store: PriceStore) {}

  async quote(req: QuoteRequest): Promise<QuoteReply> {
    return (await this.store.quote(req.userId)).toReply();
  }
}
```

### 2.3 BFF fan-out (호출 측)

```ts
@Injectable()
export class DashboardBff {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly client: ZLinkChannelClient,
  ) {}

  async load(userId: string): Promise<Dashboard> {
    const [profile, quote] = await Promise.all([
      this.client.requestToChannel('profile', new GetProfile(userId)).submit<ProfileDto>(),
      this.client.requestToChannel('pricing', new Quote(userId)).submit<QuoteDto>(),
    ]);
    return new Dashboard(profile, quote);
  }
}
```

### 2.4 분배·발견·추적 배선

```yaml
# Envoy L7 분배(서비스마다) — gRPC 는 연결이 1개라 요청 단위 분배가 필요
apiVersion: networking.istio.io/v1
kind: DestinationRule
metadata: { name: pricing }
spec: { host: pricing, trafficPolicy: { loadBalancer: { simple: ROUND_ROBIN } } }
```

```ts
// gRPC client interceptor 로 correlation id 를 전 호출에 붙인다
export const correlationInterceptor: Interceptor = (options, nextCall) =>
  new InterceptingCall(nextCall(options), {
    start(metadata, listener, next) {
      metadata.set('x-correlation-id', CurrentTrace.id());
      next(metadata, listener);
    },
  });
```

서 있어야 하는 것: 서비스별 `.proto`·gRPC stub, Envoy sidecar(서비스마다), mesh
control plane, Consul/xDS, OpenTelemetry collector + tracing 백엔드, correlation
interceptor.

## 3. ZLink 스택 — channel 이름  + Registry

```ts
@Injectable()
export class DashboardBff {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly client: ZLinkChannelClient,
  ) {}

  async load(userId: string): Promise<Dashboard> {
    const [profile, quote] = await Promise.all([
      this.client.requestToChannel('profile', new GetProfile(userId)).submit<ProfileDto>(),
      this.client.requestToChannel('pricing', new Quote(userId)).submit<QuoteDto>(),
    ]);
    return new Dashboard(profile, quote);
  }
}
```

```ts
@zlinkRequestHandler('pricing', 'Quote')
export class QuoteHandler implements ZLinkRequestHandler<Quote, QuoteDto> {
  constructor(private readonly store: PriceStore) {}

  async handle(req: Quote, context: ZLinkRequestContext): Promise<QuoteDto> {
    return this.store.quote(req.userId);
  }
}
```

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .useDiscovery()
      .addRegistryEndpoint('tcp://registry1:5551')
    .addClientServerChannel('profile')
      .enableClient()
    .addClientServerChannel('pricing')
      .enableClient()
    .build()
);
// RegistryMonitor 같은 provider는 @Module의 providers에 등록한다.
```

```ts
@Controller('admin')
export class TopologyController {
  constructor(
  ) {}

  @Get('topology')
  async topology(): Promise<Topology> {
    return this.registry.topology();
  }
}
```

```ts
// correlation 전파는 handler filter 로 공통화한다 (interceptor 자리)
@Injectable()
export class CorrelationFilter implements ZLinkHandlerFilter {
  async invoke(
    invocation: ZLinkHandlerInvocation,
    next: ZLinkHandlerDelegate,
  ): Promise<unknown> {
    const correlationId = TraceIds.current();
    return CorrelationScope.run(correlationId, () => next());
  }
}
```

> retry·circuit-breaking 은 ZLink 가 자동으로 하지 않는다(framework error 의
> retriable 플래그는 재시도 가능 여부 **힌트**일 뿐, 자동 재시도가 아니다).
> cockatiel/opossum 같은 정책 라이브러리를 호출 측에 그대로 두거나 handler filter 로
> 공통화한다 — 즉 **복원력 정책은 그대로 앱이 소유**한다.

## 4. 양쪽 코드 비교 — fan-out 한 번

| 축 | 기존(gRPC + mesh) | ZLink |
|----|-------------------|-------|
| 계약/호출 | 서비스별 생성 stub `profile.get(...)` | `client.requestToChannel('profile', ...)` channel 이름 |
| 위치/분배 | Consul/xDS + Envoy `DestinationRule` | `useDiscovery`  + Registry |
| deadline | `deadline:` 인자 | `.timeout(...)` |
| retry/circuit | cockatiel(앱) | cockatiel/filter(앱) — 동일 |

## 5. 아키텍처 비교 — 컴포넌트와 메시지 흐름

```text
[classic]  gRPC + service mesh + discovery + telemetry

  +--------+ +--------+ +--------+ +--------+
  | bff    | | profile| | pricing| |  ...   |   each pod = app + Envoy sidecar
  | +Envoy | | +Envoy | | +Envoy | | +Envoy |
  +---+----+ +---+----+ +---+----+ +---+----+
      |          |          |          |   xDS
      +----------+----+-----+----------+
              +--------v---------+
              | mesh control     |   L7 LB + mTLS
              | Consul / xDS     |   discovery
              +------------------+
  +-------------------------------+
  | OTel collector + tracing store|
  +-------------------------------+
```

```text
[ZLink]  ZLink Framework  + Registry

  +--------+ +--------+ +--------+ +--------+
  | bff    | | profile| | pricing| |  ...   |   each app + ZLink Framework
  | +ZLink | | +ZLink | | +ZLink | | +ZLink |
  +---+----+ +---+----+ +---+----+ +---+----+
      |          |          |          |   channel name
      +----------+----+-----+----------+
              +--------v---------+
              | Registry         |   discovery + topology
              +------------------+
  (tracing/metrics store stays; app feeds monitoring events into it)
```

- **빠지는 박스:** Envoy sidecar(서비스마다), mesh control plane, 별도 discovery.
- **그대로인 박스:** 추적·메트릭 백엔드(연동 지점만 monitoring 이벤트로 바뀜),
  retry/circuit 정책.

### 메시지 흐름 — 시퀀스 비교

BFF fan-out 한 번의 흐름이다.

```mermaid
sequenceDiagram
  autonumber
  participant BFF as bff
  participant M as Envoy mesh
  participant P as profile-svc
  participant Q as pricing-svc
  BFF->>M: gRPC GetProfile with deadline
  M->>P: L7 LB + mTLS
  P-->>BFF: ProfileDto
  BFF->>M: gRPC Quote with deadline
  M->>Q: L7 LB + mTLS
  Q-->>BFF: QuoteDto
  Note over BFF: cockatiel 가 재시도/서킷브레이커 처리
```

```mermaid
sequenceDiagram
  autonumber
  participant BFF as bff
  participant P as profile-svc
  participant Q as pricing-svc
  par fan-out
    BFF->>P: Request profile with timeout
    P-->>BFF: ProfileDto
  and
    BFF->>Q: Request pricing with timeout
    Q-->>BFF: QuoteDto
  end
  Note over BFF: retry/circuit 은 여전히 앱 정책
```

sidecar hop 이 빠질 뿐, fan-out 의 부분 실패·retry 정책은 양쪽 모두 앱이 진다.

## 6. 줄어드는 것 / 그대로 남는 것

- **줄어드는 것:** Envoy sidecar·mesh control plane·별도 discovery·proto/stub 관리.
- **그대로 남는 것:** retry·circuit-breaking·degrade 정책, correlation/tracing
  전파(filter 로), 외부 공개 API(REST/gRPC), 영속 DB. 공통 경계는
  [12-grpc-alternative](../12-grpc-alternative.ko.md)의 참고 절 참고.

## 7. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 사용법: [04-channel-messaging](../04-channel-messaging.ko.md), [08-registry](../08-registry.ko.md), [09-monitoring](../09-monitoring.ko.md)
- 다음 케이스: [15-case-realtime-game](15-case-realtime-game.ko.md)

---
[문서 목록](../../README.ko.md) | [이전: 케이스 — 전자상거래 체크아웃](13-case-ecommerce-checkout.ko.md) | [다음: 케이스 — 실시간 멀티플레이 게임](15-case-realtime-game.ko.md)
