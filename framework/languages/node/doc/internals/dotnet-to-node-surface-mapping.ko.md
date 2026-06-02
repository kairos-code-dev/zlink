# ZLink Framework — .NET → Node.js 표면 매핑 정책

> 이 문서는 **이식(porting) 기준 문서**다. `framework/languages/dotnet` 의 정식
> 계약과 동작을 `framework/languages/node`(NestJS) 표면으로 옮길 때, 모든 spec /
> internals 문서가 공통으로 따르는 **번역 규칙**을 한곳에 고정한다.
>
> 원칙은 하나다. **개념·의미론·동작은 그대로 두고, 표면(surface)만 바꾼다.**
> 개념의 정식 정의는 언어 중립 공통 스펙과 dotnet spec 이 소유하고, 이 문서는
> 그 의미를 Node.js / TypeScript / NestJS 모양으로만 구체화한다. 두 표기가
> 어긋나면 dotnet **코드**(`framework/languages/dotnet/src`)가 기능의 최종
> 기준이다(문서가 코드보다 뒤처질 수 있다).

## 1. 무엇을 바꾸고 무엇을 유지하는가

| 구분 | dotnet | node | 비고 |
|------|--------|------|------|
| 개념(channel, capability, packet, spot, actor, session, stream) | 동일 | 동일 | 절대 재정의하지 않는다 |
| 동작(라우팅, correlation, lifecycle, dispatch 순서) | 동일 | 동일 | 코드로 검증한다 |
| 호스트 표면 | ASP.NET Core (DI + `IHostedService`) | NestJS (`DynamicModule` + lifecycle hook) | §3 |
| 언어 표면 | C# (attribute, `ValueTask`, record) | TypeScript (decorator, `Promise`, interface) | §4 |
| 백엔드 | `bindings/dotnet` | `@zlink-systems/zlink` (Node 바인딩) | §6 |

전송 계층(transport)·ZMP·codec·논리 channel/packet 은 언어 중립 wire 계약이므로,
서로 다른 언어로 구현한 서비스가 같은 channel 위에서 상호 호출된다. 따라서 Node
버전은 **새 의미를 만들지 않는다.** dotnet 과 같은 channel/packet 으로 붙으면
양쪽이 그대로 통신한다.

## 2. 패키지·네이밍 정책

[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)
의 `Naming Policy` 를 그대로 따른다.

- 메서드·필드·함수: `camelCase` (`HandleAsync` → `handle`, `SendToChannel` → `sendToChannel`)
- 클래스·인터페이스·decorator·enum 타입: `PascalCase`
- **서버 framework public 타입은 `ZLink` prefix(대문자 `L`)** 를 쓴다.
  예: `ZLinkRequestHandler`, `ZLinkRequestContext`, `@ZLinkRequest`, `ZLinkModule`.
- **client 측 Stream Connector 패키지 타입은 `Zlink` prefix(소문자 `l`)** 를
  쓴다. 예: `ZlinkStreamConnector`, `ZlinkStreamConnectorOptions`. connector 가
  서버 framework 패키지에 의존하지 않는 독립 client 라이브러리이기 때문이다.
- 하부 zlink core C API 는 `zlink_*` snake_case 다(변경 없음).

TypeScript 에서는 C# 의 `I` prefix 인터페이스 관례를 쓰지 않는다. dotnet
`IZLinkRequestHandler<...>` → node `ZLinkRequestHandler<...>` 처럼 `I` 를 떼되
`ZLink` prefix 는 유지한다. 기존 node 드래프트가 이 규칙을 이미 따른다.

권장 패키지 구성(역순 도메인):

| 역할 | 패키지(예시) | dotnet 대응 |
|------|------|-------------|
| 코어 framework | `@zlink-systems/framework` | `Systems.Zlink.Framework` |
| NestJS 통합 | `@zlink-systems/nestjs` | `Zlink.Framework.AspNetCore` |
| Stream Connector(client) | `@zlink-systems/stream-connector` | `Systems.Zlink.Stream.Connector` |
| codec(json/msgpack/protobuf) | `@zlink-systems/stream-connector-{json,msgpack,protobuf}` | `Systems.Zlink.Stream.Connector.{Json,MessagePack,Protobuf}` |
| 하부 바인딩 | `@zlink-systems/zlink` | `bindings/dotnet` |

## 3. 호스트 매핑 — ASP.NET Core → NestJS

framework 는 새 transport 를 만들지 않고 기존 바인딩을 **호스트 모델로 감싸**
노출한다. dotnet 은 그 모델이 ASP.NET Core 의 DI + hosted service 이고, node 는
NestJS 의 module + provider + lifecycle hook 이다.

### 3.1 등록 진입점

dotnet 의 `IServiceCollection.AddZLinkFramework(options => ...)` 는 node 에서
`ZLinkModule.forRoot(options)`(동기) / `ZLinkModule.forRootAsync(...)`(비동기,
설정 주입) 가 반환하는 `DynamicModule` 로 매핑한다.

```csharp
// dotnet
builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("price", channel =>
    {
        channel.EnableServer(server => server.Bind("tcp://0.0.0.0:7301"));
        channel.AddRequestHandler<GetPriceHandler>();
    });
});
```

```ts
// node (NestJS)
@Module({
  imports: [
    ZLinkModule.forRoot({
      channels: {
        price: {
          server: { bind: 'tcp://0.0.0.0:7301' },
          requestHandlers: [GetPriceHandler],
        },
      },
    }),
  ],
  providers: [GetPriceHandler],
})
export class AppModule {}
```

builder 람다(`channel => { ... }`) 패턴은 NestJS 의 선언적 options 객체로 옮긴다.
dotnet builder 메서드 한 개 = node options 의 키 한 개로 1:1 대응시키는 것을
기본으로 한다(§5 표 참조).

### 3.2 lifecycle 매핑

dotnet framework runtime 은 `IHostedService`(`StartAsync`/`StopAsync`) 로
구동·종료된다. node 는 NestJS provider lifecycle hook 으로 매핑한다.

| dotnet (`IHostedService`) | node (NestJS hook) | 시점 |
|------|------|------|
| `StartAsync` | `onApplicationBootstrap()` | 모든 provider 준비 후 runtime 시동(bind/connect/discovery 시작) |
| `StopAsync` | `onApplicationShutdown(signal)` 또는 `onModuleDestroy()` | graceful close(linger, drain) |

runtime 시동에서 socket bind/connect 와 discovery 시작이 일어나므로, handler
provider 들이 DI 에서 모두 resolvable 한 시점(`onApplicationBootstrap`) 에 시동을
건다. lifecycle 의 정식 의미(시동 순서, 실패 처리, 종료 보장)는
[lifecycle-and-failure-semantics](./lifecycle-and-failure-semantics.ko.md) 가
소유한다.

### 3.3 DI 매핑

| dotnet (MS.DI) | node (NestJS DI) |
|------|------|
| handler 생성자 주입 | provider 생성자 주입(동일) |
| `IServiceProvider` 를 context 에 넣지 않음 | 동일. context 에 DI 컨테이너를 넣지 않는다 |
| `services.AddSingleton/AddScoped` | `providers: [...]` + scope |
| outbound client 주입(`IZLinkChannelClient`) | `@Inject(ZLINK_CHANNEL_CLIENT)` 또는 provider token 주입 |

handler 는 서비스가 필요하면 context 의 service locator 가 아니라 **생성자
주입**으로 받는다(dotnet 과 동일 원칙). framework 가 노출하는 outbound client
(`ZLinkChannelClient`, `ZLinkFanoutClient`, `ZLinkSpotManager` 등)는 NestJS
provider token 으로 주입 가능하게 등록한다.

## 4. 언어 표면 매핑 — C# → TypeScript

| dotnet | node | 규칙 |
|------|------|------|
| `ValueTask` / `ValueTask<T>` / `Task<T>` | `Promise<void>` / `Promise<T>` | async submit 기본 |
| `CancellationToken cancellationToken` | `signal?: AbortSignal` (선택 인자) 또는 생략 | handler 시그니처를 짧게 유지 |
| attribute `[ZLinkRequest]` | method decorator `@ZLinkRequest()` | §4.1 |
| `[ZLinkPacket("name")]` (class) | class decorator `@ZLinkPacket('name')` | packet key 지정 |
| `record` / `readonly record struct` | `interface` 또는 `type`(불변 객체) | DTO |
| `enum` | `enum`(문자열 값 권장) 또는 union 리터럴 | wire 값은 코드로 확인 |
| `RoutingId(string)` | branded `string`(예: `type RoutingId = string`) | rid 는 문자열 |
| `Message`, `ReadOnlyMemory<byte>` | `Message`(payload 구조 타입) / `Buffer` | payload |
| `TimeSpan period` | `periodMs: number` | 시간은 ms number |
| `IReadOnlyList<Message> createParts` | `readonly Message[]` | multipart |
| out 파라미터 / tuple | 반환 객체 | 바인딩 가이드와 일치 |

### 4.1 handler 선언 — 두 방식

dotnet 과 동일하게 **interface 방식**과 **decorator(=attribute) 방식**을 모두
지원한다.

```csharp
// dotnet — interface 방식
public sealed class GetPriceHandler : IZLinkRequestHandler<PriceRequest, PriceReply>
{
    public ValueTask<PriceReply> HandleAsync(
        PriceRequest request, ZLinkRequestContext context, CancellationToken ct)
        => ValueTask.FromResult(new PriceReply(request.Symbol, 187.42m));
}
```

```ts
// node — interface 방식
@Injectable()
export class GetPriceHandler implements ZLinkRequestHandler<PriceRequest, PriceReply> {
  async handle(request: PriceRequest, context: ZLinkRequestContext): Promise<PriceReply> {
    return { symbol: request.symbol, price: 187.42 };
  }
}
```

```ts
// node — decorator 방식 (한 클래스에 여러 역할)
@Injectable()
@ZLinkHandlerGroup()
export class PriceHandlers {
  @ZLinkRequest()
  async getPrice(request: PriceRequest, context: ZLinkRequestContext): Promise<PriceReply> {
    return { symbol: request.symbol, price: 187.42 };
  }
}
```

- interface 방식: 컴파일 타임에 시그니처를 강하게 확인한다.
- decorator 방식: 한 클래스에 여러 handler 를 모은다. 시그니처 검증은 startup
  validation 단계에서 수행한다(dotnet 과 동일).

### 4.2 handler 발견(discovery) 매핑

dotnet 은 assembly scan(`AddHandlersFromAssemblyOf<TMarker>`) + attribute /
interface 로 handler 를 **찾고**, 실제 노출은 명시적 등록(`AddHandlerGroup`,
개별 typed registration)이 정한다.

node 는 다음으로 매핑한다.

- **찾기**: NestJS `DiscoveryService` 로 provider 를 훑고, `@ZLinkHandlerGroup` /
  handler interface 구현 / method decorator 메타데이터(`reflect-metadata`) 로
  handler 후보를 모은다.
- **노출**: dotnet 과 동일하게 **scan ≠ 자동 노출**이다. 실제 channel 노출은
  module options 의 명시적 등록(`requestHandlers: [...]`,
  `handlerGroups: [...]`) 또는 `@ZLinkHandlerGroup` 의 명시 바인딩이 정한다.

자동으로 모든 handler 를 모든 channel 에 열지 않는다는 dotnet 정책을 그대로
유지한다.

### 4.3 packet key 해석 순서

dotnet 과 동일하다.

1. 호출/등록 시 지정한 `packetName`(options)
2. payload 타입의 `@ZLinkPacket('name')`
3. payload constructor(클래스) 이름 또는 schema 이름

TypeScript 는 런타임 타입 소거가 있으므로, payload 식별은 **클래스 생성자
이름** 또는 명시적 `@ZLinkPacket` / `packetName` 에 의존한다. 순수 구조적 타입
(plain interface)만 쓰는 경우 packet key 를 명시해야 한다(코드로 검증되는
제약). 이 차이는 C# 의 nominal 타입 대비 TS 의 한계이며, 의미가 아니라 표면
제약이다.

## 5. 등록 표면 대응표 (dotnet builder → node options)

dotnet `IZLinkFrameworkOptions` 의 등록 메서드를 node module options 키로
1:1 매핑한다. 각 채널 종류의 의미는 채널별 spec 이 소유한다.

| dotnet 메서드 | node options 형태 | spec |
|------|------|------|
| `AddClientServerChannel(name, ch => ...)` | `channels[name] = { server, client, requestHandlers, sendHandlers }` | [nestjs-channel-messaging](../spec/nestjs-channel-messaging.ko.md) |
| `AddFanoutChannel(name, ch => ...)` | `channels[name] = { publisher, subscriber, publishHandlers }` | nestjs-channel-messaging |
| `AddDealerMeshChannel(name, ch => ...)` | `channels[name] = { dealerMesh: {...} }` | nestjs-channel-messaging |
| `AddRouteMeshChannel(name, ch => ...)` | `channels[name] = { routeMesh: {...} }` | nestjs-channel-messaging |
| `AddSpotNode(name, sn => ...)` / `AddSpotMesh(...)` | `spotNodes[name] = {...}` / `spotMeshes[...]` | [nestjs-spot](../spec/nestjs-spot.ko.md) |
| `AddStreamNode(name, st => ...)` | `streamNodes[name] = {...}` | [nestjs-stream](../spec/nestjs-stream.ko.md) |
| `UseDiscovery(...)` | `discovery: { registries: [...] }` | [nestjs-registry](../spec/nestjs-registry.ko.md) |
| `UseFilter<TFilter>()` | `filters: [FilterClass]` | [handler-interfaces §filter](../spec/handler-interfaces.ko.md) |
| `ConfigureDispatch(...)` | `dispatch: { mode }` | handler-interfaces |
| `AddHandlersFromAssemblyOf<TMarker>()` | `discover: { modules / include }`(NestJS DiscoveryService) | §4.2 |
| `AddActorFactory(...)` | `actorFactories: [...]` | [nestjs-actor](../spec/nestjs-actor.ko.md) |
| `Codecs(reg => ...)` | `codecs: [...]` | handler-interfaces §codec |
| `ConfigureMetadata(...)` | `metadata: {...}` | nestjs-actor |

> 정확한 키 이름과 형태는 각 spec 문서가 확정한다. 위 표는 대응 관계의
> 골격이며, 실제 필드는 dotnet `Runtime/Configuration/Builders/*.cs` 를 코드로
> 확인해 채운다.

## 6. 백엔드 어댑터 매핑 (핵심 스왑 지점)

dotnet framework 의 **유일한** backend 의존은
`Runtime/Backend/Contracts/`(backend-독립 포트 인터페이스)와
`Runtime/Backend/DotNet/`(그 포트를 `bindings/dotnet` 위에 구현한 어댑터)로
격리돼 있다. Node 이식은 **포트 계약을 그대로 두고, 어댑터만 Node 바인딩으로
다시 구현**하는 작업이다.

backend port 계약(dotnet `Runtime/Backend/Contracts`):

| 포트 인터페이스 | 역할 | node 구현 대상(@zlink-systems/zlink) |
|------|------|------|
| `IZLinkBackendAdapterFactory` | 5개 어댑터를 만들어 내는 factory | `ZLinkNodeBackendAdapterFactory` |
| `IZLinkChannelBackendAdapter` | dealer/router/pub/sub socket wrapping | DealerSocket/RouterSocket/Pub/Sub |
| `IZLinkSpotBackendAdapter` | `SpotNode`/`Spot`/timer wrapping | SpotNode/Spot/Timer |
| `IZLinkStreamBackendAdapter` | stream socket wrapping | StreamSocket |
| `IZLinkRegistryBackendAdapter` | `Registry`/`RegistryQueryClient` | Registry/RegistryQueryClient |
| `IZLinkMonitoringBackendAdapter` | socket/discovery/registry/spot event source | SocketMonitor/Discovery 이벤트 |

규칙(= [backend-dependency-policy](./backend-dependency-policy.ko.md) 와 동일):

- framework public contract 가 우선이다. 바인딩 객체(`DealerSocket`, `SpotNode`,
  `Registry` 등)를 public surface 에 직접 노출하지 않는다.
- 바인딩 wrapper 생성은 **어댑터 내부에서만** 일어난다.
- public 에 남겨도 되는 primitive: `RoutingId`(string), `Message`(payload 구조 타입),
  `SendFlags`. 이들은 backend 가 바뀌어도 같은 의미를 유지한다.
- 따라서 Node backend 어댑터 구현이
  `framework/languages/dotnet/src/Zlink.Framework/Runtime/Backend/DotNet` 의
  Node 대응물이다. 이 디렉토리의 wrapper 목록(context/dealer/router/pub/sub/
  spotNode/spot/stream/registry/registryQueryClient/monitor)을 1:1 로 채운다.

backend port 의 정확한 시그니처는 dotnet
`Runtime/Backend/Contracts/*.cs` 를 코드로 읽어 Node 어댑터 구현 가이드에
옮긴다([nestjs-overview §backend](../spec/nestjs-overview.ko.md)).

## 7. 비목표 (Node 이식에서도 동일)

- 새 transport / 새 socket semantic 을 만들지 않는다. 기존 Node 바인딩
  (`DealerSocket`, `SpotNode`, `Registry` 등)을 framework 친화적으로 감쌀 뿐이다.
- handler 를 자동으로 모든 channel 에 열지 않는다(§4.2).
- backpressure 는 public no-wait 옵션이 아니라 framework 내부의 nonblocking
  send + pending queue + ready notification 으로 처리한다.

비목표의 정식 정의는
[implementation-scope-and-nongoals](./implementation-scope-and-nongoals.ko.md) 가
소유한다.

## 8. 문서별 적용 책임

이 정책은 아래 문서 전체에 공통 적용된다. 각 문서는 자기 주제의 의미를 다시
정의하지 않고, 이 표면 규칙으로만 구체화한다.

- spec: handler-interfaces, nestjs-overview, nestjs-channel-messaging,
  nestjs-spot, nestjs-actor, nestjs-stream, nestjs-registry, nestjs-monitoring,
  session-actor-dispatch, spot-node, stage-wrapper-on-spot
- internals: backend-dependency-policy, di-capability-exposure-policy,
  lifecycle-and-failure-semantics, behavior-matrix,
  implementation-scope-and-nongoals, regression-test-matrix

> 사용자 가이드(usability) 계층은 표면이 확정된 뒤 별도로 작성하며, 현재
> 구현용 draft 묶음의 범위가 아니다.

## 9. 회귀 테스트

이 표면 매핑 정책은 아래 회귀 테스트와 함께 유지한다. 테스트는 Node 표면이
dotnet 의미를 다시 정의하지 않고, 문서와 public package 선언이 같은 이름을
가리키는지 확인한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| `documentation-regression.test.js › node implementation reference docs declare regression coverage sections` | 이 정책 문서가 자기 회귀 테스트 단락을 유지한다. |
| `documentation-regression.test.js › node interface catalog names resolve in public package declarations` | guide 의 public interface catalog 이름이 실제 package declaration 에 존재한다. |
| `contract-surface.test.js` | TypeScript contract surface 가 dotnet 의미를 Node 표면으로만 옮긴 형태인지 확인한다. |
