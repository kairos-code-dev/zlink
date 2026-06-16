# Framework 설정 builder fluent 정렬 계획

> 상태: **초안**. 작성 2026-06-16.
> 목적: `cpp`, `java`, `kotlin`, `node`, `dotnet` framework 설정 코드를
> 중첩 람다 중심에서 단계가 분명한 fluent builder 중심으로 정렬한다.

이 문서는 공개 framework 설정 API를 바꾸는 실행 계획이다. 단순 샘플 정리가 아니라
공통 framework 문서, 언어별 framework 문서, 코드, 테스트, 샘플을 함께 갱신해야 한다.

## 1. 목표

설정 코드는 언어별 host 진입점의 관용은 유지하되, channel, discovery, Spot mesh 같은
하위 설정에서는 중첩 람다를 기본 방식으로 쓰지 않는다.

대표 목표 모양은 아래와 같다.

### 1.1 .NET

`.NET`에서는 `AddZLinkFramework(options => { ... })` 경계는 유지한다. 이 람다는
`IServiceCollection` 등록 시점의 설정 범위를 나타내는 .NET 관용구이기 때문이다.
그 안의 `UseDiscovery(...)`, `AddClientServerChannel(..., channel => ...)`,
`EnableServer(server => ...)` 같은 하위 설정 람다는 fluent builder로 바꾼다.

```csharp
var builder = Host.CreateApplicationBuilder();
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = SampleTimings.RequestTimeout;
    options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
    options.Codecs.AddProtobuf();

    options.UseDiscovery()
        .AddRegistryEndpoint(topology.RegistryRouterEndpoint);

    options.AddClientServerChannel(SampleNames.ApiChannel)
        .EnableServer(topology.ApiChannelEndpoint)
        .AddHandlerGroup("api");

    options.AddClientServerChannel(SampleNames.PlayChannel)
        .EnableClient();
});

return builder.Build();
```

### 1.2 C++

C++는 이미 이 방향의 API가 상당 부분 있다. 이 계획에서는 C++의 현재 sample 모양을
기준 예시로 삼고, 아직 남아 있는 callback형 설정 API와 문서 예시를 함께 정리한다.

```cpp
app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
    options.handlers ()
      .add<authenticate_player_handler_t> ("api")
      .add<match_bingo_api_handler_t> ("api");

    options.codecs ().add_protobuf ();

    options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);

    options.add_client_server_channel (sample_names_t::api_channel)
      .enable_server (topology.api_channel_endpoint)
      .use_handler_group ("api");

    options.add_client_server_channel (sample_names_t::play_channel)
      .enable_client (topology.play_channel_endpoint);
});
```

### 1.3 Java

`ZLinkFrameworkConfigurer` 람다는 Spring bean 설정 경계이므로 유지한다. 내부 설정은
반환 builder를 이어 쓰는 형태로 바꾼다.

```java
return options -> {
    options.defaultTimeout(SampleTimings.RequestTimeout);
    options.addHandlersFromPackageOf(ApiServerApplication.class);
    options.codecs().addProtobuf();

    options.useDiscovery()
        .addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);

    options.addClientServerChannel(SampleNames.ApiChannel)
        .enableServer(SampleTopology.ApiChannelEndpoint)
        .addHandlerGroup("api");

    options.addClientServerChannel(SampleNames.PlayChannel)
        .enableClient();
};
```

### 1.4 Kotlin

Kotlin도 `ZLinkFrameworkConfigurer { options -> ... }` 경계는 유지하고, 내부는 Java와
같은 의미의 fluent builder를 사용한다.

```kotlin
ZLinkFrameworkConfigurer { options ->
    options.defaultTimeout(SampleTimings.RequestTimeout)
    options.addHandlersFromPackageOf(ApiServerApplication::class.java)
    options.codecs().addProtobuf()

    options.useDiscovery()
        .addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)

    options.addClientServerChannel(SampleNames.ApiChannel)
        .enableServer(SampleTopology.ApiChannelEndpoint)
        .addHandlerGroup("api")

    options.addClientServerChannel(SampleNames.PlayChannel)
        .enableClient()
}
```

### 1.5 Node.js / TypeScript

Node.js는 현재 `zlinkFramework()` 자체가 fluent builder다. 다만
`.clientServerChannel(name, channel => ...)` callback 인자를 제거하고, channel builder를
반환하는 방식으로 정렬한다.

```ts
ZLinkModule.forRootFactory({
  useFactory: () => zlinkFramework()
    .useDiscovery()
      .addRegistryEndpoint(config.registryRouterEndpoint)
    .addClientServerChannel(SampleNames.apiChannel)
      .enableServer(config.apiEndpoint)
      .addHandlerGroup('api')
    .addClientServerChannel(SampleNames.playChannel)
      .enableClient()
    .build()
});
```

Node.js는 들여쓰기만으로 현재 channel 범위를 표현하기 어렵다. 따라서 discovery builder와
channel builder가 다음 root 등록 메서드도 함께 노출하거나, 같은 메서드 집합을 가진 타입을
반환해야 한다. `done()` 같은 종료 메서드는 추가하지 않는다.

## 2. 비목표

- `AddZLinkFramework(options => ...)`, `ZLinkFrameworkConfigurer`, `app.add_zlink_framework(...)`
  같은 host 등록 경계까지 없애지 않는다.
- `Done()`, `end()`, `parent()` 같은 상위 builder 복귀 메서드를 추가하지 않는다.
- 샘플만 새 API로 바꾸고 기존 public API를 방치하지 않는다.
- 문서 예시만 바꾸고 contract test를 갱신하지 않는 방식으로 진행하지 않는다.
- 미구현 언어의 정식 문서에 새 계약을 섞지 않는다. 아직 구현 전이면 해당 언어의
  `draft/` 또는 plan 문서에만 둔다.

## 3. 설계 원칙

구현자는 먼저 [Framework option builder naming](../../../doc/principal/framework-option-builder-naming.ko.md)을
읽고, 이 문서의 이름을 그 원칙에 맞춰 적용한다.

### 3.1 중첩 람다는 host 경계에만 남긴다

host framework가 설정 객체를 넘기는 경계는 언어 관용이므로 유지한다.

- `.NET`: `builder.Services.AddZLinkFramework(options => { ... })`
- Java/Kotlin: `ZLinkFrameworkConfigurer`
- C++: `app.add_zlink_framework(...)`
- Node.js: `ZLinkModule.forRootFactory(...)` 또는 `zlinkFramework()` root builder 생성

그 아래의 discovery, channel, Spot mesh, capability 설정은 builder 반환형으로 표현한다.

### 3.2 필수 값은 단계 메서드 인자로 받는다

서버 capability를 켜는 데 bind endpoint가 필수이면 `EnableServer(endpoint)` /
`enableServer(endpoint)` / `enable_server(endpoint)`가 대표 API가 된다. 호출자가
`EnableServer().Bind(endpoint)`처럼 두 단계를 반드시 기억해야 하는 형태는 선택 설정이
여러 개 필요한 capability에만 둔다.

### 3.3 선택 설정은 같은 builder에서 이어진다

`EnableServer(endpoint)` 뒤에는 handler group, routing id, manual connection 같은 다음
설정이 자연스럽게 이어져야 한다. 상위 builder로 돌아가기 위한 종료 메서드를 요구하지
않는다.

```csharp
options.AddClientServerChannel("api")
    .EnableServer(apiEndpoint)
    .AddHandlerGroup("api");
```

### 3.4 typestate는 빠뜨리기 쉬운 필수 단계에만 쓴다

`request`나 `send` builder처럼 필수 payload, packet name, reply type이 있는 호출은
단계별 타입을 나누는 편이 좋다. 설정 builder에서도 같은 원칙을 적용하되, 모든 선택
메서드에 과도한 타입 단계를 만들지는 않는다.

우선 적용 대상:

- server capability의 bind endpoint
- publisher capability의 bind endpoint
- route mesh server bind endpoint
- Spot mesh discovery와 node 등록 조합 중 validator가 현재 runtime에서 필수로 보는 값

### 3.5 기존 callback API는 호환성 없이 제거한다

이 작업은 호환성 유지 없이 진행한다. 새 fluent API를 추가한 뒤 기존 callback overload를
남겨 두지 않는다. 샘플, 테스트, 문서, 내부 호출 지점까지 같은 변경 범위에서 새 API로
옮긴다. 빌드가 깨지는 외부 호출자는 이번 공개 API 변경을 따라 수정해야 한다.

## 4. 공통 API 정렬표

| 개념 | C++ | Java/Kotlin | Node.js | .NET |
|------|-----|-------------|---------|------|
| Discovery | `use_discovery().add_registry_endpoint(...)` | `useDiscovery().addRegistryEndpoint(...)` | `useDiscovery().addRegistryEndpoint(...)` | `UseDiscovery().AddRegistryEndpoint(...)` |
| Client-server channel | `add_client_server_channel(name)` | `addClientServerChannel(name)` | `addClientServerChannel(name)` | `AddClientServerChannel(name)` |
| Server bind | `enable_server(endpoint)` | `enableServer(endpoint)` | `enableServer(endpoint)` | `EnableServer(endpoint)` |
| Client enable | `enable_client()` / `enable_client(endpoint)` | `enableClient()` / `enableClient(endpoint)` | `enableClient()` / `enableClient(endpoint)` | `EnableClient()` / `EnableClient(endpoint)` |
| Handler group | `use_handler_group(group)` | `addHandlerGroup(group)` | `addHandlerGroup(group)` | `AddHandlerGroup(group)` |
| Fanout publisher | `add_fanout_channel(name).enable_publisher(endpoint)` | `addFanoutChannel(name).enablePublisher(endpoint)` | `addFanoutChannel(name).enablePublisher(endpoint)` | `AddFanoutChannel(name).EnablePublisher(endpoint)` |
| Fanout subscriber | `enable_subscriber()` | `enableSubscriber()` | `enableSubscriber()` | `EnableSubscriber()` |
| Dealer mesh | `add_dealer_mesh_channel(name)` | `addDealerMeshChannel(name)` | `addDealerMeshChannel(name)` | `AddDealerMeshChannel(name)` |
| Route mesh | `add_route_mesh_channel(name)` | `addRouteMeshChannel(name)` | `addRouteMeshChannel(name)` | `AddRouteMeshChannel(name)` |
| Spot mesh | `add_spot_mesh(name)` | `addSpotMesh(name)` | `addSpotMesh(name)` | `AddSpotMesh(name)` |

이름은 각 언어의 케이싱 규칙을 따른다. 공통 의미가 같은데 언어별로 단어가 다른 경우는
이 표의 단어를 우선한다. Node.js의 기존 `server(...)`, `client()`, `handlerGroup(...)`
같은 명사형 또는 축약형 이름은 제거하고, 공통 의미가 드러나는 `enableServer(...)`,
`enableClient(...)`, `addHandlerGroup(...)`로 맞춘다.

### 4.1 제거 대상 callback API 전수 기준

host framework가 설정 객체를 넘기는 최상위 경계만 유지한다. 그 아래의 public 설정
callback은 같은 변경 범위에서 모두 fluent builder로 바꾼다. 이 계획에서 제외하는
public callback 설정 API는 없다.

| 언어 | 유지하는 host 경계 | 제거 대상 callback 설정 API |
|------|--------------------|-----------------------------|
| C++ | `app.add_zlink_framework(...)` | `app.advanced().use_zlink(std::function<...>)`, `zlink_builder_t`의 `enable_registry(...)`, `discovery(...)`, `route_channel(..., std::function<...>)`, `channel(..., std::function<...>)`, `add_spot_node(..., std::function<...>)`, `stream(..., std::function<...>)`, public framework option 또는 channel/capability builder에서 `std::function`으로 하위 설정을 받는 중복 API. 기존 fluent API로 표현 가능한 `channel(..., [] { ... })`, capability callback 예시와 문서도 제거한다. |
| Java/Kotlin | `ZLinkFrameworkConfigurer` | `configureMetadata(Consumer<...>)`, `useDiscovery(Consumer<...>)`, `add*Channel(..., Consumer<...>)`, `addSpotMesh(..., Consumer<...>)`, `addStreamNode(..., Consumer<...>)`, `useRegistrySpotRemoteAddresses(..., Consumer<...>)`, `configureDispatch(Consumer<...>)`, `configureWorkers(Consumer<...>)`, `configureRouting(Consumer<...>)`, Spot mesh의 `addNode(..., Consumer<...>)`, `attachChannelClient(..., Consumer<...>)`, `attachSpotPublisherClient(..., Consumer<...>)`, `acceptSpotRoutesFromChannel(..., Consumer<...>)`, `configureEntrySpot(Consumer<...>)`, 하위 capability의 `enable*(Consumer<...>)`, `useManualConnections(Consumer<...>)` |
| Node.js | `ZLinkModule.forRootFactory(...)`, `zlinkFramework()` root 생성 | NestJS DSL의 `clientServerChannel(name, callback)`, `fanoutChannel(name, callback)`, `routerMesh(name, callback)`, `spotNode(name, callback)`, `streamNode(name, callback)`, 그리고 그 안의 `server(...)`, `client(...)`, `publisher(...)`, `subscriber(...)`, `handlerGroup(...)` 같은 기존 명사형 설정 API |
| .NET | `builder.Services.AddZLinkFramework(options => { ... })` | `ConfigureMetadata(Action<...>)`, `UseDiscovery(Action<...>)`, `Add*Channel(..., Action<...>)`, `AddSpotMesh(..., Action<...>)`, `AddStreamNode(..., Action<...>)`, `UseRegistrySpotRemoteAddresses(..., Action<...>)`, `ConfigureDispatch(Action<...>)`, Spot mesh의 `AddNode(..., Action<...>)`, `AttachChannelClient(..., Action<...>)`, `AttachSpotPublisherClient(..., Action<...>)`, `AcceptSpotRoutesFromChannel(..., Action<...>)`, 하위 capability의 `Enable*(Action<...>)`, `ConfigureSocket(Action<...>)`, `ConfigureRouting(Action<...>)`, `UseManualConnections(Action<...>)`, `ConfigureEntrySpot(Action<...>)` |

위 표에 있는 API는 새 fluent API로 대체한다. 예를 들어 `ConfigureDispatch(...)`는
`ConfigureDispatch()`가 dispatch options builder를 반환하게 하고, `UseManualConnections(...)`는
manual endpoint builder를 반환하거나 `Connect(endpoint)` shortcut을 제공한다.
`std::function<std::shared_ptr<T>()>`나 `Supplier<T>`처럼 객체를 만드는 factory callback은
하위 설정 callback이 아니므로 이 표의 제거 대상에 포함하지 않는다. 단, factory 등록
메서드가 다시 하위 설정 builder를 callback으로 받는다면 그 설정 callback은 제거 대상이다.

## 5. 언어별 구현 계획

### 5.1 C++

현재 상태:

- `zlink_framework_options_t::use_discovery()`는 builder를 반환한다.
- `add_client_server_channel(...)`, `add_fanout_channel(...)`,
  `add_dealer_mesh_channel(...)`, `add_route_mesh_channel(...)`, `add_spot_mesh(...)`는
  fluent builder를 반환한다.
- 샘플은 이미 `enable_server(endpoint)`와 `enable_client(endpoint)` 형태를 사용한다.
- 일부 오래된 `channel(..., [] { ... })` 또는 capability callback 기반 테스트와 문서가 남아 있다.

작업:

1. public header에서 callback 기반 channel/capability 설정이 새 API와 중복되는지 확인한다.
2. 중복 API가 public 계약이면 같은 변경에서 제거한다.
3. contract header test에서 반환 타입과 chain 가능성을 고정한다.
4. unit test와 sample parity test에서 callback 예시를 fluent 예시로 바꾼다.
5. `framework/languages/cpp/doc/{guide,spec,internals}`의 설정 예시를 모두 갱신한다.

검증:

- `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(contract_headers|layout_contract|sample_parity)' --output-on-failure`
- C++ sample runner가 있으면 `framework/languages/cpp/samples/run_samples.sh`
- `rg -U "use_zlink\\s*\\([^;]*std::function|enable_registry\\s*\\([^;]*std::function|discovery\\s*\\([^;]*std::function|route_channel\\s*\\([^;]*std::function|channel\\s*\\([^;]*std::function|add_spot_node\\s*\\([^;]*std::function|stream\\s*\\([^;]*std::function|enable_router\\s*\\([^;]*std::function|enable_pub_sub\\s*\\([^;]*std::function|accept_routes_from_channel\\s*\\([^;]*std::function|attach_channel_client\\s*\\([^;]*std::function|attach_publisher\\s*\\([^;]*std::function|configure_dispatch\\s*\\([^;]*std::function|\\.(enable_registry|discovery|route_channel|channel|add_spot_node|stream|enable_router|enable_pub_sub|accept_routes_from_channel|attach_channel_client|attach_publisher|configure_dispatch)\\s*\\([^;]*(\\[|\\{)" framework/languages/cpp`
  결과가 없어야 한다. `add_zlink_framework(...)` host 경계와 내부 deferred action 저장소는 이 금지 패턴에 포함하지 않는다.

### 5.2 Java / Kotlin

현재 상태:

- `ZLinkFrameworkOptions`는 `useDiscovery(Consumer<...>)`,
  `addClientServerChannel(name, Consumer<...>)`, `addSpotMesh(name, Consumer<...>)`
  중심이다.
- Java와 Kotlin 샘플 모두 중첩 람다를 사용한다.

작업:

1. `ZLinkFrameworkOptions`에 builder 반환 메서드를 추가한다.
   - `ZLinkDiscoveryBuilder useDiscovery()`
   - `ClientServerChannelBuilder addClientServerChannel(String name)`
   - `FanoutChannelBuilder addFanoutChannel(String name)`
   - `DealerMeshChannelBuilder addDealerMeshChannel(String name)`
   - `RouteMeshChannelBuilder addRouteMeshChannel(String name)`
   - `ZLinkSpotMeshBuilder addSpotMesh(String name)`
2. channel capability builder에 필수값 shortcut을 추가한다.
   - `enableServer(String endpoint)`
   - `enableClient(String endpoint)`가 수동 연결 하나를 뜻하는지, 기존 discovery client와 충돌하지 않는지 검토한다.
   - fanout publisher, dealer mesh server, route mesh server도 같은 규칙을 적용한다.
3. §4.1의 Java/Kotlin 제거 대상 `Consumer` overload를 모두 제거한다.
4. `zlink-framework-kotlin` 확장과 Kotlin compile fixture가 새 Java builder 반환 타입을
   자연스럽게 사용할 수 있는지 확인한다.
5. Java 샘플과 Kotlin 샘플을 모두 새 fluent API로 바꾼다.
6. Spring Boot starter test, core integration test, fake backend test의 설정 코드를 갱신한다.
7. Java/Kotlin 문서의 spec, guide, samples, internals 예시를 모두 갱신한다.

검증:

- `framework/languages/java`의 Gradle build/test
- `zlink-framework-testkit` contract/fake backend tests
- `zlink-framework-kotlin` compile/test
- Java/Kotlin sample runner
- `rg "(configureMetadata|useDiscovery|configureDispatch|configureWorkers|configureRouting|configureEntrySpot)\\s*(\\{|\\([^\\n]*->)|add(ClientServer|Fanout|DealerMesh|RouteMesh)Channel\\([^\\n]+,|add(ClientServer|Fanout|DealerMesh|RouteMesh)Channel\\([^\\n]*\\)\\s*\\{|addSpotMesh\\([^\\n]+,|addSpotMesh\\([^\\n]*\\)\\s*\\{|addStreamNode\\([^\\n]+,|addStreamNode\\([^\\n]*\\)\\s*\\{|addNode\\([^\\n]+,|addNode\\([^\\n]*\\)\\s*\\{|useRegistrySpotRemoteAddresses\\([^\\n]+,|enable(Server|Client|Publisher|Subscriber|Router|PubSub)\\s*(\\{|\\([^\\n]*->)|(attachChannelClient|attachSpotPublisherClient|acceptSpotRoutesFromChannel|useManualConnections)\\s*(\\{|\\([^\\n]*->)|attachChannelClient\\([^\\n]+,|attachSpotPublisherClient\\([^\\n]+,|acceptSpotRoutesFromChannel\\([^\\n]+," framework/languages/java`
  결과가 없어야 한다. host 경계인 `ZLinkFrameworkConfigurer` 람다는 이 금지 패턴에 포함하지 않는다.

### 5.3 Node.js / TypeScript

현재 상태:

- `zlinkFramework()`는 fluent builder지만 `clientServerChannel(name, channel => ...)`
  callback 형태가 샘플에 남아 있다.
- NestJS module test와 sample regression test가 현재 샘플 형태를 검사한다.

작업:

1. `packages/framework/src/contracts/Configuration/Builders.ts`와
   `packages/nestjs/src/index.ts`에서 root builder와 channel
   builder 반환 타입을 재설계한다.
2. `addClientServerChannel(name)`이 channel builder를 반환하되, 그 반환 타입에서 다음 root
   channel도 이어서 추가할 수 있게 한다. `done()`은 추가하지 않는다.
3. 현재 NestJS DSL의 `clientServerChannel(name, callback)`, `server(endpoint)`,
   `client()`, `handlerGroup(group)`을 제거하고 `addClientServerChannel(name)`,
   `enableServer(endpoint)`, `enableClient()`, `addHandlerGroup(group)`로 바꾼다.
4. §4.1의 Node.js 제거 대상 callback builder를 모두 제거한다.
5. Node 샘플 `Bingo.Ts`, `TicTacToe.Ts`의 module 설정을 모두 새 체인으로 바꾼다.
6. `nestjs-module.test.js`, `sample-regression.test.js`, `contract-surface.test.js`,
   `documentation-regression.test.js`를 새 API에 맞춘다.
7. Node 문서의 spec, guide, samples, internals, implementation plan 예시를 모두 갱신한다.

검증:

- `npm run build`
- `npm run typecheck`
- `node --test test/contract/contract-surface.test.js`
- `node --test test/contract/nestjs-module.test.js`
- `node --test test/contract/sample-regression.test.js`
- `npm run verify:samples`
- `rg "clientServerChannel\\(|fanoutChannel\\([^\\n]+,|routerMesh\\([^\\n]+,|spotNode\\([^\\n]+,|streamNode\\([^\\n]+,|\\.server\\(|\\.client\\(|\\.publisher\\(|\\.subscriber\\(|\\.handlerGroup\\(" framework/languages/node`
  결과가 없어야 한다.

### 5.4 .NET

현재 상태:

- `IZLinkFrameworkOptions`와 하위 builder가 `Action<TBuilder>` 중심이다.
- `AddZLinkFramework(options => { ... })` 자체는 유지할 host 설정 경계다.
- 샘플과 unit/e2e test에 `UseDiscovery(discovery => ...)`,
  `AddClientServerChannel(name, channel => ...)`, `EnableServer(server => ...)`가 넓게 쓰인다.

작업:

1. `IZLinkFrameworkOptions`에 builder 반환 메서드를 추가한다.
   - `IZLinkDiscoveryBuilder UseDiscovery()`
   - `IZLinkClientServerChannelBuilder AddClientServerChannel(string channelName)`
   - `IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName)`
   - `IZLinkDealerMeshChannelBuilder AddDealerMeshChannel(string channelName)`
   - `IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName)`
   - `IZLinkSpotMeshBuilder AddSpotMesh(string channelName)`
2. 하위 builder의 capability 메서드를 chain 가능하게 바꾼다.
   - `EnableServer(string endpoint)`
   - `EnableClient()`
   - `EnableClient(string endpoint)`가 필요하면 수동 연결 shortcut으로 정의한다.
   - `EnablePublisher(string endpoint)`, `EnableSubscriber()`,
     route/dealer mesh의 server/client shortcut도 함께 정렬한다.
3. `ConfigureSocket(...)`, `ConfigureRouting(...)`, `UseManualConnections(...)` 같은 선택 설정은
   builder 자신을 반환하게 바꾼다.
4. §4.1의 .NET 제거 대상 `Action<TBuilder>` overload를 모두 제거한다. 한 가지 방식만 남기는 것이 목표이므로
   샘플과 문서는 새 API만 사용한다.
5. `framework/languages/dotnet/samples` 전체를 새 API로 바꾼다.
6. unit/e2e test 설정 코드를 갱신한다.
7. `Documentation/Regression.cs`와 sample regression test에서 새 API를 검사한다.
8. `.NET` guide/spec/case-study 문서의 모든 설정 예시를 갱신한다.

검증:

- `dotnet test framework/languages/dotnet/Zlink.Framework.sln` 또는 현재 solution gate
- solution gate가 기존 이슈로 불안정하면 project별 unit/e2e/sample gate를 분리해서 기록한다.
- `rg "ConfigureMetadata\\([^\\n]*=>|UseDiscovery\\([^\\n]*=>|Add(ClientServer|Fanout|DealerMesh|RouteMesh)Channel\\([^\\n]+,|AddSpotMesh\\([^\\n]+,|AddStreamNode\\([^\\n]+,|AddNode\\([^\\n]+,|Attach(ChannelClient|SpotPublisherClient)\\([^\\n]+,|AcceptSpotRoutesFromChannel\\([^\\n]+,|UseRegistrySpotRemoteAddresses\\([^\\n]+,|ConfigureDispatch\\([^\\n]*=>|Enable(Server|Client|Publisher|Subscriber|Router|PubSub)\\([^\\n]*=>|Configure(Socket|Routing|Publisher|Subscriber|EntrySpot)\\([^\\n]*=>|UseManualConnections\\([^\\n]*=>" framework/languages/dotnet`
  결과가 없어야 한다. host 경계인 `AddZLinkFramework(options => ...)` 람다는 이 금지 패턴에 포함하지 않는다.

## 6. 문서 수정 범위

문서 수정은 코드 변경과 같은 PR 범위에 포함한다. 아래 위치를 빠짐없이 확인한다.

### 6.1 공통 framework 문서

- `framework/doc/README.ko.md`
- `framework/doc/spec/README.ko.md`
- `framework/doc/spec/framework-api.ko.md`
- `framework/doc/spec/channel-topology.ko.md`
- `framework/doc/spec/actor-model.ko.md`
- `framework/doc/spec/session-actor-dispatch.ko.md`
- `framework/doc/spec/sample/README.ko.md`
- `framework/doc/spec/sample/**/README.ko.md`
- `framework/doc/spec/use-cases/**/*.ko.md`
- `framework/doc/spec/draft/*.ko.md` 중 framework 설정 예시가 있는 문서
- `doc/principal/framework-option-builder-naming.ko.md`

공통 문서는 API 이름의 의미와 언어별 매핑을 다룬다. 언어별 세부 시그니처를 길게 반복하지
않고, 대표 예시는 짧게 둔 뒤 언어별 문서로 연결한다.

### 6.2 C++ framework 문서

- `framework/languages/cpp/doc/README.ko.md`
- `framework/languages/cpp/doc/spec/*.ko.md`
- `framework/languages/cpp/doc/guide/*.ko.md`
- `framework/languages/cpp/doc/internals/*.ko.md`
- `framework/languages/cpp/samples/**/*.md`

### 6.3 Java/Kotlin framework 문서

- `framework/languages/java/doc/README.ko.md`
- `framework/languages/java/doc/spec/*.ko.md`
- `framework/languages/java/doc/guide/**/*.ko.md`
- `framework/languages/java/doc/internals/*.ko.md`
- `framework/languages/java/doc/draft/**/*.ko.md`
- `framework/languages/java/samples/**/*.md`

Java와 Kotlin 샘플이 같은 framework API를 공유하므로 문서 예시는 둘 다 갱신한다.

### 6.4 Node.js framework 문서

- `framework/languages/node/doc/README.ko.md`
- `framework/languages/node/doc/spec/*.ko.md`
- `framework/languages/node/doc/guide/**/*.ko.md`
- `framework/languages/node/doc/internals/*.ko.md`
- `framework/languages/node/doc/draft/**/*.ko.md`
- `framework/languages/node/samples/**/*.md`

### 6.5 .NET framework 문서

- `framework/languages/dotnet/doc/README.ko.md`
- `framework/languages/dotnet/doc/guide/**/*.ko.md`
- `framework/languages/dotnet/doc/spec/**/*.ko.md`
- `framework/languages/dotnet/samples/**/*.md`
- `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Documentation/Regression.cs`

`.NET` 문서를 바꾸면 documentation regression도 함께 갱신한다.

## 7. 테스트 추가 및 갱신 기준

### 7.1 공개 API 테스트

각 언어는 새 fluent 설정 API가 public API로 노출되는지 확인해야 한다.

- C++: compile-time `static_assert`로 반환 타입과 chain 가능성 확인
- Java: interface compile test 또는 starter/core unit test에서 반환 타입 사용
- Kotlin: sample compile 또는 dedicated Kotlin compile fixture
- Node.js: TypeScript typecheck와 `contract-surface.test.js`
- .NET: public interface compile test 또는 unit test에서 반환 타입 사용

### 7.2 Behavior regression test

새 builder는 기존 registration 결과와 같은 runtime registration을 만들어야 한다.

확인할 항목:

- channel server bind endpoint
- channel client discovery 또는 manual connection
- handler group mapping
- fanout publisher/subscriber 설정
- dealer mesh server/client 설정
- route mesh server/client 설정
- Spot mesh discovery, node, router, pub/sub, attached channel client 설정
- duplicate name과 빈 endpoint validation

### 7.3 Sample parity test

샘플은 새 API의 대표 사용처다. 각 언어의 sample regression 또는 release gate에서 아래를
검사한다.

- 유지 대상 샘플이 새 fluent 설정만 사용한다.
- 중첩 하위 설정 람다가 남아 있지 않다.
- API/Play/Session 같은 서버 역할별 설정 모양이 언어별로 같은 의미를 가진다.
- sample runner가 실제로 성공한다.

### 7.4 Documentation regression

문서 회귀 테스트는 단순 문자열 존재가 아니라 이전 callback 예시의 잔존 여부도 확인해야 한다.

예시 금지 패턴:

- `.NET`: `UseDiscovery(discovery =>`, `AddClientServerChannel(..., channel =>`,
  `EnableServer(server =>`
- Java/Kotlin: `useDiscovery(discovery ->`, `addClientServerChannel(..., channel ->`,
  `enableServer(server ->`
- Node.js: `clientServerChannel(name, (channel) =>`
- C++: 새 API로 대체된 callback capability 예시

## 8. 실행 순서

권장 순서는 아래와 같다.

1. C++ 현재 API를 기준으로 공통 framework 문서의 목표 모양을 먼저 갱신한다.
   Node.js 이름은 이 문서의 공통 정렬표대로 `enableServer` / `enableClient`를 사용한다.
2. 공통 문서와 언어별 문서에서 기존 callback 예시가 어디에 있는지 `rg`로 목록화한다.
3. .NET public interface와 runtime builder를 바꾸고, unit/e2e/sample을 갱신한다.
4. Java public interface와 runtime builder를 바꾸고, Java/Kotlin sample과 Gradle test를 갱신한다.
5. Node.js builder 타입을 바꾸고, sample과 contract tests를 갱신한다.
6. C++에 남은 callback 중복 API와 문서 예시를 정리한다.
7. 공통 문서와 언어별 문서를 다시 grep으로 훑어 오래된 예시를 제거한다.
8. 전체 언어별 sample gate를 실행한다.
9. 작업자가 자체 최종 review를 수행해 설정 API가 한 가지 방식만 남았는지 확인한다.
10. 별도 Codex 에이전트에 review를 요청해 누락, 잘못된 API 이름, 문서와 코드 불일치,
    테스트 누락을 점검한다.
11. Codex review에서 이슈가 나오면 수정하고, 같은 review 요청을 다시 수행한다.
    review 결과가 더 이상 이슈를 내지 않을 때까지 반복한다.
12. review 이슈가 모두 닫히면 [POSD 설계 원칙](../../../doc/principal/software-design-principles.md)에
    따라 언어별 framework 리팩토링 단계를 진행한다.

## 9. 완료 기준

완료 판정은 아래 항목을 모두 만족해야 한다.

- 공통 framework 문서가 fluent 설정 API를 기준으로 설명한다.
- C++, Java, Kotlin, Node.js, .NET 샘플이 모두 새 설정 API를 사용한다.
- 각 언어별 framework 문서가 새 예시와 새 public 계약을 반영한다.
- 기존 중첩 하위 설정 람다는 public 문서와 샘플에서 사라진다.
- 각 언어의 public API 테스트가 새 반환 타입과 chain 가능성을 검증한다.
- 각 언어의 runtime behavior test가 기존 registration 의미가 유지됨을 검증한다.
- 각 언어의 sample runner가 통과한다.
- `rg` 기반 금지 패턴 검색 결과가 없다. 남은 결과가 있으면 작업은 완료가 아니다.
- 별도 Codex 에이전트 review에서 누락이나 잘못된 변경이 없다는 결과를 받았다.
- Codex review에서 이슈가 나온 경우 수정 후 재리뷰를 반복했고, 마지막 review 결과에
  새 이슈가 없다.
- 언어별 framework POSD 리팩토링 계획과 실행 결과가 기록되었다.
- `git diff --check`가 통과한다.

## 10. Codex review 요청 기준

구현 완료 후 작업자는 별도 Codex 에이전트에 아래 범위로 review를 요청한다. 이 review는
코드 수정 요청이 아니라 누락과 잘못된 변경을 찾는 검증 단계다.

review 요청에는 아래 내용을 반드시 포함한다.

- 이번 변경의 목표: 중첩 하위 설정 람다 제거, fluent 설정 API 단일화, 호환성 없는 기존
  callback overload 제거
- 대상 언어: `framework/languages/cpp`, `framework/languages/java`,
  `framework/languages/java/zlink-framework-kotlin`, `framework/languages/java/samples/kotlin`,
  `framework/languages/node`, `framework/languages/dotnet`
- 대상 문서: `framework/doc/spec`, `framework/doc/README.ko.md`,
  각 언어의 `doc/{guide,spec,internals,draft}`, 각 언어의 sample README
- 확인할 금지 패턴: 이 문서 §7.4와 각 언어별 검증 절의 `rg` 패턴
- 확인할 테스트: public API compile/type test, registration behavior regression,
  documentation regression, sample runner
- 출력 형식: 발견한 이슈를 심각도 순서로 쓰고, 각 이슈는 `file:line` 근거와
  수정 방향을 포함한다. 이슈가 없으면 "이슈 없음"과 남은 위험만 짧게 적는다.

Codex review에서 나온 이슈는 같은 변경 범위에서 수정한다. 수정 후에는 같은 조건으로
다시 review를 요청한다. 마지막 review가 새 이슈를 내지 않아야 이 계획의 API 정렬 작업이
끝난 것으로 본다.

## 11. 언어별 POSD 리팩토링 단계

Codex review에서 더 이상 이슈가 없으면 언어별 framework를 POSD 기준으로 다시 본다.
이 단계는 fluent API 적용 과정에서 생긴 중복, 얕은 builder, 패스스루 메서드, 시간적 분해를
줄이기 위한 후속 리팩토링이다.

언어별로 아래 절차를 반복한다.

1. 위험 신호 목록을 먼저 작성한다.
   - 인터페이스가 구현보다 복잡한 builder
   - 단순히 내부 registration 객체에 전달만 하는 public 메서드
   - root builder와 channel builder 사이에 중복된 상태 변경 코드
   - validation 지식이 여러 builder에 흩어진 경우
   - 문서 예시를 맞추기 위해 만든 얕은 wrapper
2. 각 위험 신호가 어떤 POSD 원칙을 어기는지 적는다.
   - 깊은 모듈
   - 정보 은닉
   - 복잡성을 아래로
   - 오류를 정의로 없애기
3. 수정 방향을 두 가지 이상 비교한다.
   첫 번째 안을 바로 구현하지 않고, 호출자 API가 더 단순해지는 쪽을 선택한다.
4. public API에 영향이 있으면 호출자 코드가 더 짧고 명확해졌는지 샘플로 확인한다.
5. 리팩토링 후 같은 언어의 공개 API 테스트, behavior regression, sample runner를 다시 실행한다.
6. 위험 신호가 해소되었는지 재점검하고, 남은 항목은 알려진 이슈로 숨기지 않고 기록한다.

언어별 리팩토링 범위는 아래와 같다.

| 언어 | 우선 확인할 영역 | 필수 검증 |
|------|------------------|-----------|
| C++ | `framework/include/zlink/framework/contracts/configuration`, `framework/src/runtime/channels`, sample parity tests | contract header test, channel/registry runtime tests, sample runner |
| Java/Kotlin | `zlink-framework-core` configuration interfaces/runtime builders, Spring Boot starter configurer 경계, Java/Kotlin samples | Gradle unit/integration/contract tests, Java/Kotlin sample runner |
| Node.js | `packages/framework/src/contracts/Configuration`, `packages/framework/src/contracts/Configuration/Registration.ts`, `packages/nestjs/src/index.ts` | `npm run build`, `npm run typecheck`, contract tests, sample runner |
| .NET | `Contracts/Configuration/Builders.cs`, runtime configuration builders, ASP.NET Core service registration 경계 | unit/e2e tests, documentation regression, sample runner |

POSD 리팩토링은 API 정렬 작업을 되돌리거나 두 번째 설정 방식을 다시 추가하는 명분이
될 수 없다. 리팩토링 결과도 새 fluent 설정 API 하나만 남겨야 한다.

### 11.1 실행 기록

최종 Codex review에서 새 이슈가 없음을 확인한 뒤 아래 기준으로 언어별 framework를 다시 보았다.

위험 신호:

- Node.js `Registration.ts`에 더 이상 사용하지 않는 capability builder 잔재가 남아 있었다.
  이 코드는 새 fluent API가 직접 endpoint를 받도록 바뀐 뒤 호출 경로가 없어졌고, 얕은 wrapper와
  과거 설계 지식을 남기는 문제였다.
- Node.js 기본 protobuf serializer의 varint 인코딩이 작은 `number[]` 임시 배열을 만들었다.
  사용자 payload를 배열로 확장하지는 않았지만, 새 기본 codec 경로이므로 반복 할당을 줄이는 쪽이
  성능 기준에 더 맞다.
- Java `SpotBuilders`에 fluent builder 구현 포맷이 한 군데 흐트러져 있었다. 동작 문제는 아니지만
  sample/public API 정리 작업 뒤 남겨둘 이유가 없는 잡음이었다.

검토한 대안:

- 기존 코드를 그대로 두고 문서에만 현재 제약을 적는 안은 폐기했다. 미사용 builder와 임시 배열은
  호출자가 보지 않아도 내부 복잡성을 유지한다.
- 새 추상화를 추가해서 언어별 builder 구현을 다시 감싸는 안도 폐기했다. 이미 fluent API가 단일
  표면으로 정리되었기 때문에, 새 helper는 얕은 모듈을 늘릴 가능성이 컸다.
- 실제로 쓰이지 않는 코드를 제거하고, 기본 codec의 작은 할당을 줄이며, 포맷 잡음만 바로잡는 안을
  선택했다. 이 방식은 public API를 바꾸지 않고 새 설정 표면 하나만 유지한다.

실행 결과:

- Node.js의 미사용 capability builder 잔재를 제거했다.
- Node.js 기본 protobuf serializer의 varint 인코딩을 고정 크기 Buffer 기반으로 바꿔 작은 배열
  할당을 없앴다.
- Java `SpotBuilders.addNode` 포맷을 정리했다.
- .NET, C++, Java/Kotlin의 fluent 설정 표면은 이미 기존 검증과 review에서 endpoint 누락이나
  구 API 잔재가 없음을 확인했기 때문에, 이 단계에서 추가 public API 변경은 하지 않았다.

## 12. 구현 전 체크리스트

구현자는 작업 시작 전에 아래를 먼저 확인한다.

- `framework/doc/spec/framework-api.ko.md`에서 현재 설정 API 설명 위치
- `framework/doc/spec/channel-topology.ko.md`에서 discovery와 channel 연결 설명
- 각 언어의 builder interface 파일
- 각 언어의 sample regression test
- 각 언어의 documentation regression test
- 각 언어 sample runner 명령

이 계획은 설정 API를 단순화하기 위한 작업이다. 구현 중 특정 언어에서 fluent chain을
맞추기 위해 오히려 호출자가 더 많은 상태를 기억해야 한다면, 그 언어는 먼저 공통 이름을
기계적으로 맞추지 말고 builder 반환 타입을 다시 설계한다.
