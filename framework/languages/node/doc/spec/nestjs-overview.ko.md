# ZLink Framework Node.js — NestJS Overview (부트스트랩 · DI · Lifecycle · Backend 어댑터)

[문서 목록](../README.ko.md) | [표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md) | [handler-interfaces](./handler-interfaces.ko.md)

[DI 노출 정책](../internals/di-capability-exposure-policy.ko.md) | [Lifecycle/Failure](../internals/lifecycle-and-failure-semantics.ko.md) | [Backend 의존 정책](../internals/backend-dependency-policy.ko.md)

> 이 문서는 [표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md)을
> 따른다. 개념·의미론·동작은 `framework/languages/dotnet` 과 **동일**하고,
> 호스트 표면만 NestJS 의 `DynamicModule` + provider lifecycle hook 으로, 언어
> 표면만 TypeScript(`Promise`, `interface`, decorator)로 옮긴다. 표기가
> 어긋나면 `framework/languages/dotnet/src` **코드**가 기능의 최종 기준이다.
>
> .NET 에 1:1 단일 대응 문서가 없는 **신규 spec** 이다. .NET 의
> `Zlink.Framework.AspNetCore`(host integration) + `Runtime/Host`(runtime
> 시동·종료) + `Runtime/Configuration`(등록 진입점) + `Runtime/Backend/Contracts`
> (backend 포트)를 합성해 NestJS 표면으로 옮긴 것이다.

---

## 1. 목적 — 부트스트랩 척추 + backend 스왑 지점

framework 는 새 transport 를 만들지 않는다. 기존 Node 바인딩
(`@zlink-systems/zlink` 의 `DealerSocket`, `RouterSocket`, `SpotNode`,
`Registry` 등)을 **호스트 모델로 감싸** NestJS 애플리케이션이 선언적으로 쓰도록
노출할 뿐이다. .NET 에서 그 호스트 모델이 ASP.NET Core 의 DI +
`IHostedService` 이고, Node 에서는 NestJS 의 module + provider + lifecycle hook
이다.

이 문서가 닫는 두 가지 골격은 다음과 같다.

1. **부트스트랩 척추** — `ZLinkModule.forRoot/forRootFactory` 가 `DynamicModule` 을
   만들고, provider 를 등록하고, lifecycle hook(`onApplicationBootstrap` /
   `onApplicationShutdown`)으로 runtime 을 시동·종료한다. 이 골격 위에
   handler-interfaces 의 계약을 얹으면 runtime spine 이 완성된다.
2. **backend 스왑 지점** — framework 의 **유일한** backend 의존은 6개의 backend
   포트 인터페이스(§5)다. .NET 은 이 포트를 `bindings/dotnet` 위에 구현하고,
   Node 는 같은 포트를 `@zlink-systems/zlink` 위에 다시 구현한다. 포트 계약을
   그대로 두고 어댑터만 갈아 끼우는 것이 이식의 핵심이다.

구현 진입 순서: 이 overview(부트스트랩 + backend 포트) → handler-interfaces(계약)
→ backend 어댑터(포트 구현) → 각 subsystem spec(channel/spot/actor/stream/
registry/monitoring). §6 에서 다시 정리한다.

---

## 2. 모듈 부트스트랩

### 2.1 등록 진입점 — `ZLinkModule.forRoot` / `forRootFactory`

.NET 의 `IServiceCollection.AddZLinkFramework(options => ...)`(빌더 람다)를
NestJS 에서는 `ZLinkModule.forRoot(options)`(동기) / `ZLinkModule.forRootFactory(
{ useFactory, inject, imports })`(비동기, 설정 주입)가 반환하는
`DynamicModule` 로 매핑한다.
이 `DynamicModule` 은 `@nestjs/common` 의 실제 모듈/provider 타입으로 만든다.
framework runtime 과 client 는 NestJS DI 컨테이너가 resolve 하며, runtime 시작과
종료는 NestJS lifecycle hook 을 통해 처리한다.

.NET 빌더 메서드(`AddClientServerChannel`, `AddSpotNode`, `UseDiscovery` …)
**한 개** = NestJS options 객체의 **키 한 개**로 1:1 대응시키는 것을 기본으로
한다(키 표는 [표면 매핑 정책 §5](../internals/dotnet-to-node-surface-mapping.ko.md)).

```ts
// node (NestJS) — 동기 등록
import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework, zlinkHandlers } from '@zlink-systems/nestjs';

@Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .options({ defaultTimeoutMs: 30_000 })
        .clientServerChannel('pricing.quote', (channel) => channel
          .server('tcp://0.0.0.0:7301')
          .handlerGroup('pricing'))
        .fanoutChannel('pricing.events', (channel) => channel
          .publisher('tcp://0.0.0.0:7302'))
        .build()
    ),
  ],
  providers: [
    ...zlinkHandlers('pricing')
      .request(GetQuoteHandler, 'GetQuote')
      .providers(),
  ],
})
export class PricingModule {}
```

```csharp
// dotnet 대응 — AddZLinkFramework
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(30);
    options.AddClientServerChannel("pricing.quote", channel =>
    {
        channel.EnableServer(server => server.Bind("tcp://0.0.0.0:7301"));
        channel.AddRequestHandler<GetQuoteHandler>();
    });
    options.AddFanoutChannel("pricing.events", channel =>
    {
        channel.EnablePublisher(pub => pub.Bind("tcp://0.0.0.0:7302"));
    });
});
```

`forRootFactory` 는 설정값이 `ConfigService` 같은 다른 provider 에서 와야 할 때
쓴다. .NET 에 정확한 대응은 없지만(`AddZLinkFramework` 는 동기), NestJS 의
표준 async module 패턴을 그대로 따른다.

```ts
// node (NestJS) — 비동기 등록(설정 주입)
@Module({
  imports: [
    ConfigModule,
    ZLinkModule.forRootFactory({
      imports: [ConfigModule],
      inject: [ConfigService],
      useFactory: (config: ConfigService) => zlinkFramework()
        .clientServerChannel('pricing.quote', (channel) => channel
          .server(config.getOrThrow<string>('PRICING_BIND'))
          .handlerGroup('pricing'))
        .build(),
    }),
  ],
  providers: [
    ...zlinkHandlers('pricing')
      .request(GetQuoteHandler, 'GetQuote')
      .providers(),
  ],
})
export class PricingModule {}
```

> **검증 시점 약속**: 설정 검증은 lifecycle hook 이 아니라 **module 등록 시점**
> 에 끝난다. .NET 은 `AddZLinkFramework(...)` 호출 안에서
> `ZLinkFrameworkRegistrationValidator.Validate(...)` 를 돌린다. NestJS 도
> `forRoot(...)` 에서는 `DynamicModule` 을 만들기 전에 검증을 수행하고,
> `forRootFactory(...)` 에서는 NestJS 가 factory 를 실행해 옵션을 받은 직후
> registration 을 만든다. 잘못된 registration 조합·필수 endpoint 누락은
> runtime start 전에 실패한다([lifecycle §2~3](../internals/lifecycle-and-failure-semantics.ko.md)).

### 2.2 Registry / Registry Query Client 모듈

.NET 은 `AddZLinkFramework` 와 별개로 `AddZLinkRegistry`(embedded registry 구동)
와 `AddZLinkRegistryQueryClient`(원격 registry 조회 전용 client)를 둔다. Node
도 별도 module 로 매핑한다.

| .NET 확장 메서드 | node module | 역할 |
|------|------|------|
| `services.AddZLinkRegistry(reg => ...)` | `ZLinkRegistryModule.forRoot(options)` / `forRootFactory(...)` | embedded Registry 를 bind·구동하고 `ZLINK_REGISTRY_QUERY` provider 노출 |
| `services.AddZLinkRegistryQueryClient(c => ...)` | `ZLinkRegistryQueryClientModule.forRoot(options)` / `forRootFactory(...)` | 원격 Registry 에 connect 해 topology 조회만 하는 client(`ZLINK_REGISTRY_QUERY_CLIENT`) |
| `services.AddZLinkMonitoring(m => ...)` | `ZLinkMonitoringModule.forRoot(options)` | runtime/registry/spot/socket 이벤트 source attach([nestjs-monitoring](./nestjs-monitoring.ko.md)) |

```ts
// node — embedded Registry
@Module({
  imports: [
    ZLinkRegistryModule.forRoot({
      registryId: 1,
      pubEndpoint: 'tcp://0.0.0.0:7401',
      routerEndpoint: 'tcp://0.0.0.0:7402',
      heartbeatIntervalMs: 1_000,
      heartbeatTimeoutMs: 5_000,
      peers: ['tcp://registry-2.internal:7402'],
    }),
  ],
})
export class RegistryModule {}
```

```ts
// node — 원격 Registry 조회 전용 client
@Module({
  imports: [
    ZLinkRegistryQueryClientModule.forRoot({
      endpoint: 'tcp://registry-1.internal:7402',
    }),
  ],
})
export class TopologyDashboardModule {}
```

Registry module 과 Registry query client module 도 `forRootFactory({ imports,
inject, useFactory })` 를 지원한다. 설정 provider 에서 endpoint 를 읽어야 할 때
NestJS 표준 async module 패턴과 같은 방식으로 쓴다.

> embedded registry 와 framework runtime 이 같은 프로세스에 공존할 때의 시동·
> 종료 순서(registry 먼저 시동, framework 뒤 — 종료는 그 역순)는 §4 와
> [lifecycle §2,4](../internals/lifecycle-and-failure-semantics.ko.md) 가
> 소유한다. registry 의 정식 표면은 [nestjs-registry](./nestjs-registry.ko.md)
> 가 확정한다.

---

## 3. provider / DI 노출

framework 가 노출하는 outbound client 와 manager 는 NestJS **provider token**
(`InjectionToken`)으로 주입 가능하게 등록한다. `@Inject(TOKEN)` 으로 받으며,
token 은 framework 가 export 한다. handler 는 context 의 service locator 가
아니라 **생성자 주입**으로 의존을 받는다(.NET 과 동일 원칙. context 에 DI
컨테이너를 넣지 않는다).

NestJS 통합에서 application 이 구현하는 다음 객체는 NestJS DI 컨테이너가
소유한다. 이 객체들은 `main.ts` 같은 부트스트랩 코드에서 직접 `new` 로 만들지
않고 module `providers` 에 등록한다.

| 객체 종류 | 등록 위치 | framework 가 resolve 하는 시점 |
|------|------|------|
| channel / fanout / route handler | `providers` + `zlinkHandlers(...).request/send/publish(...).providers()` | channel 이 해당 handler group 을 dispatch 할 때 |
| Entry Spot, user Spot | `providers` + `spotNodes` 의 spot type 설정 | SpotNode 또는 SpotManager 가 spot 을 활성화할 때 |
| Spot packet / subscribe / actor / timer handler | `providers` + Spot/Entry Spot 의 registry 등록 | 해당 Spot 실행 문맥에서 packet, actor event, timer 를 처리할 때 |
| actor factory | `providers` + `actorFactories` 설정 | ActorManager 가 actor 를 생성할 때 |
| stream session 또는 session factory | `providers` + `streams` 설정 | stream 연결을 session 으로 활성화할 때 |
| custom Spot remote address resolver | `providers` + resolver type 설정 | Spot outbound 가 remote address 를 해석할 때 |

`ZLinkModule.forRoot(...)` 는 transport, node, capability, handler group 선택을
선언하는 자리다. application 객체 그래프를 조립하는 자리가 아니다. node/channel
handler 는 `zlinkHandlers(...)` builder 로 group 이름을 붙이고 channel 이
`handlerGroups` 로 선택한다. 반면 Spot, Entry Spot, session 내부 handler 는
해당 객체의 registry 에 등록한다. 이렇게 나누면 channel 이 어떤 node handler
묶음을 받을지와, Spot 또는 session 이 자기 내부 메시지를 어떻게 처리할지가 서로
섞이지 않는다.

예외적으로 actor 인스턴스, per-connection transport adapter, protocol header 같은
런타임 값 객체는 NestJS provider 가 아니다. 이런 객체는 DI 로 관리되는 factory
또는 framework runtime 이 만든다. 중요한 기준은 application service, handler,
factory, Spot, session 처럼 의존성을 받는 확장 지점은 NestJS provider 여야 한다는
점이다.

핵심 원칙은 **주입 가능성 = 기능 가능성**이다. 어떤 capability 도 등록하지
않았는데 그 service 를 주입받을 수 있으면 안 된다. 따라서 일부 provider 는
capability 조건이 충족될 때만 `providers`/`exports` 에 들어간다. 정식 정의는
[di-capability-exposure-policy](../internals/di-capability-exposure-policy.ko.md)
가 소유한다. 아래는 .NET `ZLinkFrameworkServiceRegistrar.AddPublicClients(...)`
의 등록 조건을 옮긴 요약이다.

`forRootFactory(...)` 와 handler discovery 를 쓰는 `forRoot(...)` 는 registration 이
DI 단계에서 확정되기 전에는 어떤 capability 가 필요한지 알 수 없다. 그래서 이 두
경로는 capability 토큰을 export 하되, 해당 capability 가 없는 경우 provider 값은
`null` 이다. 이 정책은 NestJS application context 가 optional capability 때문에
부팅 단계에서 실패하지 않게 하기 위한 것이다.

### 3.1 항상 등록되는 provider

| provider token | 주입 타입 | .NET 대응 | capability 누락 시 |
|------|------|------|------|
| `ZLINK_CHANNEL_CLIENT` | `ZLinkChannelClient` | `IZLinkChannelClient` | 없는 channel/client capability 호출 시 `ZLinkConfigurationException` |
| `ZLINK_ROUTE_CLIENT` | `ZLinkRouteClient`(`ZLinkMultipartRouteClient` 포함) | `IZLinkRouteClient`/`IZLinkMultipartRouteClient` | route channel 없으면 호출 시 `ZLinkConfigurationException` |
| `ZLINK_FANOUT_CLIENT` | `ZLinkFanoutClient` | `IZLinkFanoutClient` | publisher capability 없으면 호출 시 `ZLinkConfigurationException` |
| `ZLINK_BOUND_SESSION_FACTORY` | `ZLinkBoundSessionFactory` | `IZLinkBoundSessionFactory` | binding 없는 actor 호출 시 `ActorSessionNotBound` |
| `ZLINK_MESSAGE_METADATA_POLICY` | `ZLinkMessageMetadataPolicy` | `IZLinkMessageMetadataPolicy` | 항상 유효 |

### 3.2 capability 가 있을 때만 등록되는 provider

| provider token | 주입 타입 | 등록 조건(.NET) | 미등록 시 |
|------|------|------|------|
| `ZLINK_SPOT_MANAGER` | `ZLinkSpotManager` | `SpotNode` 1개 이상(`HasSpotNode`) | DI resolve 실패(`UnknownDependenciesException`) |
| `ZLINK_SPOT_OUTBOUND` | `ZLinkSpotOutbound` | `SpotNode` 1개 이상 | DI resolve 실패 |
| `ZLINK_SPOT_PUBLISHER_CLIENT` | `ZLinkSpotPublisherClient` | attached spot publisher client 1개 이상(`HasSpotPublisherClient`) | DI resolve 실패 |
| `ZLINK_ACTOR_MANAGER` | `ZLinkActorManager` | `SpotNode` 1개 이상 **그리고** actor factory 1개 이상 | DI resolve 실패 |
| `ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER` | `ZLinkSpotRemoteAddressResolver` | `spot.remoteAddressResolver` 또는 registry remote address 구성 | 조건 미충족 시 미등록 |

handler / filter / actor factory / spot factory / stream header session 타입은
.NET 과 동일하게 적절한 scope 로 등록한다(handler·filter 는 transient,
spot/actor/stream session 은 spot/session scope). 자세한 scope 매핑과 생성자
주입 발견(.NET `ZLinkApplicationDependencyRegistrar`)은 handler-interfaces 와
각 subsystem spec 이 다룬다.

---

## 4. lifecycle

NestJS provider lifecycle hook 으로 .NET `IHostedService` 를 매핑한다.

| .NET (`IHostedService`) | node (NestJS hook) | 시점 |
|------|------|------|
| `StartAsync(ct)` | `onApplicationBootstrap()` | 모든 provider 가 DI 에서 resolvable 해진 뒤 runtime 시동(bind/connect/discovery) |
| `StopAsync(ct)` | `onApplicationShutdown(signal?)`(또는 `onModuleDestroy()`) | graceful close(linger, drain) |

`onApplicationBootstrap()` 시점에 시동하는 이유는, socket bind/connect 와
discovery 시작이 일어나려면 handler provider 들이 모두 resolvable 해야 하기
때문이다.

### 4.1 시동 순서

.NET 은 `ZLinkRegistryHostedService` → `ZLinkFrameworkHostedService` →
`ZLinkMonitoringHostedService` 세 hosted service 가 같은 순서로 `StartAsync`
된다. Node 도 **register → framework → monitoring** 순서의 lifecycle 참여자로
구성한다.

`onApplicationBootstrap()` 안의 세부 시동 순서(.NET `ZLinkFrameworkRuntime
.StartAsync` → state factory 생성 순서):

1. embedded registry 가 있으면 **먼저** 시동(`ZLinkFrameworkRuntime.StartAsync`
   가 자기 state 를 만들기 전에 `_registryRuntime.StartAsync` 를 호출)
2. backend channel adapter 로 `Context` 생성(`CreateContext`)
3. inbound(server) channel → publisher channel → client channel → route(mesh)
   channel 초기화
4. spot node 초기화
5. stream node 초기화
6. monitoring source attach

> **idempotent 시동**: framework/registry runtime 의 시동은 idempotent 해야
> 한다(.NET `ZLinkFrameworkRuntime` 은 `_gate` 락 안에서 `_state is not null`
> 이면 즉시 return). monitoring hook 이 같은 runtime 을 다시 시동시켜도 두 번
> 시작되지 않는다.

### 4.2 종료 순서(graceful close)

종료는 시동의 역순으로 본다. monitoring detach → framework runtime state
dispose → embedded registry stop → `Context` dispose.

framework runtime state dispose 세부 순서(.NET `ZLinkFrameworkRuntimeState
.DisposeAsync`):

1. stop token cancel 후 listener task drain
2. spot node dispose
3. route(mesh) channel dispose
4. spot discovery dispose
5. stream node dispose
6. client → publisher → subscriber → server channel bundle dispose
7. 마지막에 `Context` dispose

> **embedded registry 종료 위임**: .NET `ZLinkRegistryHostedService.StopAsync`
> 는 framework runtime 이 함께 있는 구성에서는 **no-op** 이고, 실제 registry
> stop 을 framework runtime 의 stop 경로(`ZLinkFrameworkRuntime.StopAsync` 가
> 자기 state dispose 후 `_registryRuntime.StopAsync` 호출)가 책임진다. Node 도
> embedded registry 가 framework 와 함께 구동될 때는 registry 종료를 framework
> 종료 경로에 위임해 "framework 먼저, registry 나중" 순서를 지킨다. registry
> 단독 구성일 때만 registry 종료 hook 이 직접 stop 한다.

### 4.3 fail-fast

startup 단계에서 runtime state 를 만들다가 한 컴포넌트라도 생성에 실패하면,
그때까지 만든 state 를 **그 자리에서 dispose 한 뒤 예외를 다시 던진다.** 반쯤
열린 socket 이나 매달린 `Context` 를 남기지 않는다. 시동/종료/실패의 정식
의미는 [lifecycle-and-failure-semantics](../internals/lifecycle-and-failure-semantics.ko.md)
가 소유한다.

---

## 5. backend 어댑터 포트 — 6개 인터페이스 (이식의 심장)

framework 의 **유일한** backend 의존은 .NET
`Runtime/Backend/Contracts/`(backend-독립 포트)와
`Runtime/Backend/DotNet/`(그 포트를 `bindings/dotnet` 위에 구현한 어댑터)로
격리돼 있다. Node 이식은 **포트 계약을 그대로 두고, 어댑터만
`@zlink-systems/zlink` 로 다시 구현**하는 작업이다.

### 5.1 변환 규칙(이 절 전체에 적용)

- C# `internal interface IZLink...` → TypeScript `interface ZLink...`
  (`I` prefix 제거, `ZLink` prefix 유지).
- `ValueTask` / `ValueTask<T>` → `Promise<void>` / `Promise<T>`.
- `CancellationToken cancellationToken` → `signal?: AbortSignal`.
- `TimeSpan` / `TimeSpan?` → `number`(ms) / `number | undefined`(`timeoutMs`).
- `IReadOnlyList<T>` → `readonly T[]`; `ReadOnlySpan<byte>` → `Buffer`/`Uint8Array`.
- `Action` → `() => void`; `Action<T>` → `(arg: T) => void`;
  콜백 delegate(`RequestCallback`, `ActorJoinCallback` …)는 동일 시그니처의
  함수 타입으로 옮긴다.
- `.NET` / C++ 의 coroutine 계열 이름에 붙는 `Async` suffix 는 Node public API 로
  옮기지 않는다. 비동기 실행의 공통 의미는
  [framework 공통 정책](../../../../doc/spec/async-execution-policy.ko.md)을 따르고,
  Node에서는 `Promise<T>` 반환 타입과 `await` 사용이 비동기 계약이다.
  예: `SubmitAsync<T>` → `submit<T>()`, `HandleAsync` → `handle()`.
- 서버와 client 의 socket, stream, channel, registry, actor, Spot lifecycle
  함수는 동기 쌍을 따로 두지 않고 `Promise` 기반 함수만 제공한다.
- `out bool created` 같은 out 파라미터는 **반환 객체**로 옮긴다
  (예: `{ spot, created }`).
- `RoutingId`(string), `Message`(payload 구조 타입), `SendFlags`, `RecvFlags`
  는 backend 가 바뀌어도 같은 의미를 유지하는 primitive 라 public 에 남는다.
- 모든 backend 객체 인터페이스는 .NET 의 `IZLinkBackendObject`(`NativeInstance`)
  + `IAsyncDisposable` 을 따른다. Node 에서는 공통 base
  `interface ZLinkBackendObject { readonly nativeInstance: unknown }` +
  `dispose(): Promise<void>`(async dispose) 로 옮긴다.

> 바인딩 wrapper 생성은 **어댑터 내부에서만** 일어난다. 바인딩 객체
> (`DealerSocket`, `SpotNode`, `Registry`)를 framework public surface 에 직접
> 노출하지 않는다([backend-dependency-policy](../internals/backend-dependency-policy.ko.md)).

### 5.2 포트 6개 요약

| 포트 인터페이스(node) | .NET 원본 | 역할 | Node 구현 대상(`@zlink-systems/zlink`) |
|------|------|------|------|
| `ZLinkBackendAdapterFactory` | `IZLinkBackendAdapterFactory` | 5개 어댑터를 만드는 factory | `ZLinkNodeBackendAdapterFactory` |
| `ZLinkChannelBackendAdapter` | `IZLinkChannelBackendAdapter` | context/discovery + dealer/router/pub/sub socket 생성 | Context/Discovery/DealerSocket/RouterSocket/Pub/Sub |
| `ZLinkSpotBackendAdapter` | `IZLinkSpotBackendAdapter` | `SpotNode` 생성 | SpotNode |
| `ZLinkStreamBackendAdapter` | `IZLinkStreamBackendAdapter` | stream socket 생성 | StreamSocket |
| `ZLinkRegistryBackendAdapter` | `IZLinkRegistryBackendAdapter` | `Registry` / `RegistryQueryClient` 생성 | Registry/RegistryQueryClient |
| `ZLinkMonitoringBackendAdapter` | `IZLinkMonitoringBackendAdapter` | socket monitor 열기 | SocketMonitor |

### 5.3 `ZLinkBackendAdapterFactory`

5개 어댑터를 만들어 내는 factory. framework runtime 은 이 factory 하나만 주입
받는다(.NET `ZLinkFrameworkRuntime` 생성자의
`IZLinkBackendAdapterFactory backendAdapterFactory`).

```ts
interface ZLinkBackendAdapterFactory {
  createChannelAdapter(): ZLinkChannelBackendAdapter;
  createSpotAdapter(): ZLinkSpotBackendAdapter;
  createStreamAdapter(): ZLinkStreamBackendAdapter;
  createRegistryAdapter(): ZLinkRegistryBackendAdapter;
  createMonitoringAdapter(): ZLinkMonitoringBackendAdapter;
}
```

### 5.4 `ZLinkChannelBackendAdapter`

`Context`, `Discovery`, dealer/router/publisher/subscriber socket 생성을
담당한다. `Context` 는 다른 모든 backend 객체의 소유 루트다.

```ts
interface ZLinkChannelBackendAdapter {
  createContext(): ZLinkBackendContext;
  createDiscovery(
    context: ZLinkBackendContext,
    autoConnectType: ZLinkAutoConnectType,
    channelName: string,
  ): ZLinkBackendDiscovery;
  createDealerSocket(context: ZLinkBackendContext): ZLinkBackendDealerSocket;
  createRouterSocket(context: ZLinkBackendContext): ZLinkBackendRouterSocket;
  createPublisherSocket(context: ZLinkBackendContext): ZLinkBackendPublisherSocket;
  createSubscriberSocket(context: ZLinkBackendContext): ZLinkBackendSubscriberSocket;
}

interface ZLinkBackendContext extends ZLinkBackendObject {
  shutdown(): void;
  dispose(): Promise<void>; // IAsyncDisposable
}
```

### 5.5 `ZLinkSpotBackendAdapter`

```ts
interface ZLinkSpotBackendAdapter {
  createSpotNode(
    context: ZLinkBackendContext,
    mode: SpotNodeMode,
  ): ZLinkBackendSpotNode;
}
```

### 5.6 `ZLinkStreamBackendAdapter`

```ts
interface ZLinkStreamBackendAdapter {
  createStreamSocket(context: ZLinkBackendContext): ZLinkBackendStreamSocket;
}
```

### 5.7 `ZLinkRegistryBackendAdapter`

```ts
interface ZLinkRegistryBackendAdapter {
  createRegistry(context: ZLinkBackendContext): ZLinkBackendRegistry;
  createRegistryQueryClient(
    context: ZLinkBackendContext,
  ): ZLinkBackendRegistryQueryClient;
}
```

### 5.8 `ZLinkMonitoringBackendAdapter`

```ts
interface ZLinkMonitoringBackendAdapter {
  openSocketMonitor(socket: ZLinkBackendSocket): ZLinkBackendSocketMonitor;
}
```

### 5.9 어댑터가 만들어 내는 backend 객체 인터페이스

위 6개 포트가 생성하는 객체들도 같은 변환 규칙으로 옮긴 backend-독립 인터페이스
다. Node 어댑터는 이들 wrapper 도 함께 구현해야 한다(.NET
`Runtime/Backend/Contracts/IZLinkBackendSocketContracts.cs`,
`IZLinkBackendSpotContracts.cs`, `IZLinkBackendRegistryContracts.cs`).

#### Discovery / Socket (`IZLinkBackendSocketContracts`)

```ts
interface ZLinkBackendDiscovery extends ZLinkBackendObject {
  spotOwnerSyncEnabled: boolean;
  actorRouteSyncEnabled: boolean;
  connectRegistry(endpoint: string): void;
  memberPeers(): readonly ZLinkMemberPeerEntry[];
  resolveSpot(spotRid: RoutingId): SpotRoute;
  resolveActor(actorId: string): ActorRoute;
  bindRoute(kind: number, key: Buffer, value: Buffer): void;
  unbindRoute(kind: number, key: Buffer): void;
  resolveRoute(kind: number, key: Buffer): ZLinkBackendDiscoveryRoute;
  dispose(): Promise<void>;
}

// 공통 socket base
interface ZLinkBackendSocket extends ZLinkBackendObject {
  bind(endpoint: string): void;
  setChannelName(channelName: string): void;
  dispose(): Promise<void>;
}
interface ZLinkBackendConnectableSocket extends ZLinkBackendSocket {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
}

interface ZLinkBackendDealerSocket extends ZLinkBackendConnectableSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  onSendReady(handler: () => void): void;
  send(message: Message, flags: SendFlags): boolean;
  send(parts: readonly Message[], flags: SendFlags): boolean;
  request(
    message: Message | readonly Message[],
    callback: RequestCallback,
    flags: SendFlags,
    timeoutMs?: number,
  ): boolean;
  recv(flags?: RecvFlags): Received | undefined;
}

interface ZLinkBackendRouterSocket extends ZLinkBackendConnectableSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  onSendReady(handler: () => void): void;
  setRoutingId(routingId: RoutingId): void;
  recv(flags?: RecvFlags): Received | undefined;
  send(routingId: RoutingId, message: Message, flags: SendFlags): boolean;
  send(routingId: RoutingId, parts: readonly Message[], flags: SendFlags): boolean;
  request(
    routingId: RoutingId,
    message: Message | readonly Message[],
    callback: RequestCallback,
    flags: SendFlags,
    timeoutMs?: number,
  ): boolean;
  sendToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    parts: readonly Message[],
    flags: SendFlags,
  ): boolean;
  requestToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    parts: readonly Message[],
    callback: RequestCallback,
    flags: SendFlags,
    timeoutMs?: number,
  ): boolean;
  reply(routingId: RoutingId, requestSeq: bigint, message: Message): void;
  reply(routingId: RoutingId, requestSeq: bigint, parts: readonly Message[]): void;
}

interface ZLinkBackendPublisherSocket extends ZLinkBackendSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  onSendReady(handler: () => void): void;
  publish(topic: string, message: Message, flags: SendFlags): boolean;
  publish(topic: string, parts: readonly Message[], flags: SendFlags): boolean;
}

interface ZLinkBackendSubscriberSocket extends ZLinkBackendConnectableSocket {
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  setSubscription(topic: string): void;
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
}

interface ZLinkBackendStreamSocket extends ZLinkBackendSocket {
  onFramedPacket(handler: (peer: string, header: Message, payload: Message) => void): void;
  send(routingId: RoutingId, payload: Message, flags: SendFlags): boolean;
  send(routingId: RoutingId, parts: readonly Message[], flags: SendFlags): boolean;
  disconnectPeer(routingId: RoutingId): void;
  attachActorGateway(node: ZLinkBackendSpotNode): void;
  bindActor(
    sessionRid: RoutingId,
    actor: ZLinkBackendActorRef,
    timeoutMs: number,
    signal?: AbortSignal,
  ): Promise<void>;
  unbindActor(
    sessionRid: RoutingId,
    actorId: string,
    timeoutMs: number,
    signal?: AbortSignal,
  ): Promise<void>;
  sendBoundActor(
    sessionRid: RoutingId,
    actorId: string,
    parts: readonly Message[],
    flags: SendFlags,
  ): boolean;
}

interface ZLinkBackendSocketMonitor extends ZLinkBackendObject {
  onEvent(handler: (event: ZLinkBackendSocketMonitorEvent) => void): void;
  recv(): ZLinkBackendSocketMonitorEvent;
  dispose(): Promise<void>;
}
```

#### SpotNode / Spot (`IZLinkBackendSpotContracts`)

```ts
interface ZLinkBackendSpotNode extends ZLinkBackendObject {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  setRouterBind(endpoint: string): void;
  setPubBind(endpoint: string): void;
  attachDiscovery(discovery: ZLinkBackendDiscovery): void;
  connectPeer(endpoint: string): void;
  disconnectPeer(endpoint: string): void;
  connectRouterChannelPeer(channelName: string, endpoint: string): void;
  connectRouterChannelPeerRid(channelName: string, peerRid: RoutingId, endpoint: string): void;
  disconnectRouterChannelPeer(channelName: string, endpoint: string): void;
  disconnectRouterChannelPeerRid(channelName: string, peerRid: RoutingId): void;
  attachSpotRouteChannelDiscovery(channelName: string, discovery: ZLinkBackendDiscovery): void;
  createSpot(): ZLinkBackendSpot;
  getOrCreateSpot(spotRid: RoutingId): { spot: ZLinkBackendSpot; created: boolean }; // out bool → 반환 객체
  status(): ZLinkSpotNodeStatus;
  peers(): readonly ZLinkSpotNodePeerEntry[];
  subjects(): readonly ZLinkSpotNodeSubjectEntry[];
  attachChannelDealer(discovery: ZLinkBackendDiscovery, dealer: ZLinkBackendDealerSocket): void;
  attachChannelDealerManual(channelName: string, dealer: ZLinkBackendDealerSocket): void;
  entrySpot(): ZLinkBackendSpot;
  createActor(actorId: string): ZLinkBackendActorRef;
  actorLookup(actorId: string): ZLinkBackendActorRef | undefined;
  joinActor(
    actor: ZLinkBackendActorRef,
    destNodeRid: RoutingId,
    destSpotRid: RoutingId,
    payload: Message | readonly Message[],
    callback: RequestCallback | ActorJoinCallback,
    timeoutMs?: number,
  ): boolean;
  joinActorEntrySpot(
    actor: ZLinkBackendActorRef,
    destNodeRid: RoutingId,
    callback: ActorJoinEntrySpotCallback,
    timeoutMs?: number,
  ): boolean;
  destroyActor(actor: ZLinkBackendActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void>;
  sendActorBoundSession(actor: ZLinkBackendActorRef, parts: readonly Message[], flags: SendFlags): boolean;
  closeActorBoundSession(actor: ZLinkBackendActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void>;
  dispose(): Promise<void>;
}

interface ZLinkBackendSpot extends ZLinkBackendObject {
  readonly routingId: RoutingId;
  setRoutingId(routingId: RoutingId): void;
  setSubscription(topic: string): void;
  subscribe(result: TopicMessage, flags: RecvFlags): boolean;
  recvRoute(result: Received, flags: RecvFlags): boolean;
  onDispatchEvent(handler: (info: ZLinkBackendSpotDispatchInfo) => void): void;
  onSendReady(handler: () => void): void;
  requestToChannel(
    channelName: string,
    payload: Message | readonly Message[],
    callback: RequestCallback,
    flags: SendFlags,
    timeoutMs?: number,
  ): boolean;
  sendToChannel(channelName: string, payload: Message | readonly Message[], flags: SendFlags): boolean;
  publish(topic: string, payload: Message | readonly Message[], flags: SendFlags): boolean;
  sendToSpot(
    targetRid: RoutingId,
    spotRid: RoutingId,
    payload: Message | readonly Message[],
    flags: SendFlags,
  ): boolean;
  requestToSpot(
    targetRid: RoutingId,
    spotRid: RoutingId,
    payload: Message | readonly Message[],
    callback: RequestCallback,
    flags: SendFlags,
    timeoutMs?: number,
  ): boolean;
  recvActorJoin(flags: RecvFlags): ZLinkBackendActorJoinRequest | undefined;
  replyActorJoin(
    request: ZLinkBackendActorJoinRequest,
    joinResultCode: number,
    reply: Message | readonly Message[],
  ): void;
  dispose(): Promise<void>;
}
```

> `joinActor` / `requestToChannel` / `sendToChannel` / `publish` / `sendToSpot`
> / `requestToSpot` / `reply` / `replyActorJoin` 은 public framework 표면에서
> 단일 `Message` 요청/응답을 우선 사용한다. transport 내부가 여러 part 를 쓰더라도
> application callback 계약은 별도 part list 를 직접 받지 않는다.

#### Registry (`IZLinkBackendRegistryContracts`)

```ts
interface ZLinkBackendRegistry extends ZLinkBackendObject {
  setId(registryId: number): void;
  setHeartbeat(intervalMs: number, timeoutMs: number): void;
  setBroadcastInterval(intervalMs: number): void;
  addPeer(endpoint: string): void;
  bind(pubEndpoint: string, routerEndpoint: string): void;
  status(): ZLinkRegistryStatus;
  serviceSummary(filter?: ZLinkRegistryServiceSummaryFilter): readonly ZLinkRegistryServiceSummaryEntry[];
  topology(filter?: ZLinkRegistryTopologyFilter): readonly ZLinkRegistryTopologyEntry[];
  memberPeers(channelName: string): readonly ZLinkMemberPeerEntry[];
  dispose(): Promise<void>;
}

interface ZLinkBackendRegistryQueryClient extends ZLinkBackendObject {
  connect(endpoint: string): void;
  topology(filter?: ZLinkRegistryTopologyFilter): readonly ZLinkRegistryTopologyEntry[];
  dispose(): Promise<void>;
}
```

### 5.10 Node 어댑터가 채워야 하는 wrapper 집합

.NET `Runtime/Backend/DotNet/` 디렉토리의 wrapper 목록을 Node 어댑터가 1:1 로
채운다. `@zlink-systems/zlink` 바인딩 객체를 위 포트로 감싼다.

| wrapper | 감싸는 바인딩 객체 | 포트 인터페이스 |
|------|------|------|
| context | `Context` | `ZLinkBackendContext` |
| discovery | `Discovery` | `ZLinkBackendDiscovery` |
| dealer | `DealerSocket` | `ZLinkBackendDealerSocket` |
| router | `RouterSocket` | `ZLinkBackendRouterSocket` |
| publisher | `PublisherSocket`(Pub) | `ZLinkBackendPublisherSocket` |
| subscriber | `SubscriberSocket`(Sub) | `ZLinkBackendSubscriberSocket` |
| spotNode | `SpotNode` | `ZLinkBackendSpotNode` |
| spot | `Spot` | `ZLinkBackendSpot` |
| stream | `StreamSocket` | `ZLinkBackendStreamSocket` |
| registry | `Registry` | `ZLinkBackendRegistry` |
| registryQueryClient | `RegistryQueryClient` | `ZLinkBackendRegistryQueryClient` |
| monitor | `SocketMonitor` | `ZLinkBackendSocketMonitor` |

이 12개 wrapper + 5개 어댑터 + 1개 factory(`ZLinkNodeBackendAdapterFactory`)가
backend 스왑 지점의 전부다. framework 의 다른 어떤 코드도 `@zlink-systems/zlink`
를 직접 import 하지 않는다.

---

## 6. 구현 순서

1. **overview(이 문서)** — 부트스트랩 척추(`ZLinkModule.forRoot/forRootFactory`,
   provider/lifecycle wiring)와 backend 포트 6개를 고정한다.
2. **backend 어댑터** — §5 의 포트를 `@zlink-systems/zlink` 위에 구현한다
   (`ZLinkNodeBackendAdapterFactory` + 12개 wrapper). 이것이 **유일한** backend
   스왑 지점이다.
3. **handler-interfaces** — [handler-interfaces](./handler-interfaces.ko.md) 의
   계약(handler interface, decorator, context, options)을 TypeScript 로 옮긴다
   (backend 독립).
4. **subsystem spec** — channel messaging → spot → actor → stream → registry →
   monitoring 순서로 각 spec 을 수직 슬라이스로 구현한다
   ([nestjs-channel-messaging](./nestjs-channel-messaging.ko.md),
   [nestjs-spot](./nestjs-spot.ko.md), [nestjs-actor](./nestjs-actor.ko.md),
   [nestjs-stream](./nestjs-stream.ko.md), [nestjs-registry](./nestjs-registry.ko.md),
   [nestjs-monitoring](./nestjs-monitoring.ko.md)).
5. **회귀 검증** — [regression-test-matrix](../internals/regression-test-matrix.ko.md)
   로 .NET 과 동등성을 확인한다.

---

## 7. 회귀 테스트

이 overview 는 bootstrap, DI, lifecycle, backend adapter 경계의 기준 문서다.
아래 테스트가 이 문서의 핵심 결정을 고정한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| `backend-contract.test.js` | backend adapter factory 가 channel, spot, stream, registry, monitoring adapter 를 모두 제공한다. |
| `backend-public-api-only.test.js` | framework runtime 이 binding internal/native 경로를 직접 import 하지 않는다. |
| `nestjs-module.test.js` | `ZLinkModule.forRoot/forRootFactory`, provider token 노출, startup validation, 실제 NestJS application context 주입, lifecycle 연결이 동작한다. |
| `documentation-regression.test.js › node implementation reference docs declare regression coverage sections` | 이 overview 가 자기 회귀 테스트 단락을 유지한다. |

[문서 목록](../README.ko.md) | [표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md) | [DI 노출 정책](../internals/di-capability-exposure-policy.ko.md) | [Lifecycle/Failure](../internals/lifecycle-and-failure-semantics.ko.md)
