<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [Java draft](../README.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- .NET -> Java/Kotlin Surface Mapping Policy

> 이 문서는 **구현 전 초안**이다.
> `framework/languages/dotnet`의 계약과 동작을 Java/Kotlin/Spring Boot 표면으로
> 옮길 때 모든 Java draft 문서가 따르는 번역 규칙을 한곳에 고정한다.

## 1. 유지하는 것과 바꾸는 것

| 구분 | `.NET` | Java/Kotlin | 규칙 |
|------|--------|-------------|------|
| 개념 | channel, capability, Spot, actor, session, stream | 동일 | 재정의하지 않는다 |
| 동작 | routing, correlation, lifecycle, dispatch 순서 | 동일 | 회귀 테스트로 검증한다 |
| host 표면 | ASP.NET Core DI + hosted service | Spring Boot bean + lifecycle | host 모델만 바꾼다 |
| 언어 표면 | C# attribute, `ValueTask`, record | Java annotation, `CompletionStage`, record/class | 언어 관례만 바꾼다 |
| Kotlin 표면 | 없음 | suspend, Flow, DSL wrapper | Java runtime 위 thin wrapper |
| backend | `bindings/dotnet` | `bindings/java` | public binding API만 호출 |

전송 계층, ZMP, codec, 논리 channel/packet은 언어 중립 계약이다. 따라서 Java
framework는 새로운 wire 의미를 만들지 않는다. `.NET`과 같은 channel과 packet으로
붙으면 양쪽이 그대로 통신해야 한다.

> async 표면 결정: framework public async 반환은 `CompletionStage<T>`로 고정한다.
> Reactor `Mono`/`Flux`(또는 RxJava) 표면을 자동으로 노출하는 것은 비목표다.
> 필요하면 Kotlin coroutine wrapper처럼 별도 thin wrapper에서 다룬다.

## 2. 패키지와 네이밍

module artifact 와 package 는 아래를 정확히 쓴다. binding group 은 `systems.zlink`
다. connector client 타입은 `Zlink`(소문자 `l`) prefix를 쓴다(§2.1).

| 역할 | Java module(artifact) | package | `.NET` 대응 |
|------|-----------------------|---------|-------------|
| core framework | `zlink-framework-core` | `systems.zlink.framework` | `Systems.Zlink.Framework` |
| Spring Boot starter | `zlink-framework-spring-boot-starter` | `systems.zlink.framework.spring` | `Zlink.Framework.AspNetCore` |
| Stream Connector(client) | `zlink-stream-connector` | `systems.zlink.stream.connector` | `Systems.Zlink.Stream.Connector` |
| connector codec helper | `zlink-stream-connector-codecs`/`-json`/`-msgpack`/`-protobuf` | `systems.zlink.stream.connector.*` | `Systems.Zlink.Stream.Connector.{Codecs,Json,MessagePack,Protobuf}` |
| Kotlin wrapper | `zlink-framework-kotlin` | `systems.zlink.framework.kotlin` | 없음 |

Java 메서드는 `camelCase`, class/interface/annotation/enum은 `PascalCase`를 쓴다.
`.NET`의 `I` prefix는 Java public interface 이름에 옮기지 않는다. 단 `ZLink`
prefix(대문자 `L`)는 그대로 유지한다.

### 2.1 server framework 타입 prefix vs connector client prefix

- **server framework public 타입은 `ZLink` prefix(대문자 `L`)** 를 쓴다.
  예: `ZLinkRequestHandler`, `ZLinkRequestContext`, `@ZLinkRequest`,
  `ZLinkSpotManager`, `@EnableZLinkFramework`.
- **client 측 Stream Connector 타입은 `Zlink` prefix(소문자 `l`)** 를 쓴다.
  예: `ZlinkStreamConnector`, `ZlinkStreamConnectorOptions`. connector가 server
  framework 모듈에 의존하지 않는 독립 client 라이브러리이기 때문이다.
- 하부 zlink core C API는 `zlink_*` snake_case 그대로다(변경 없음).

### 2.2 메서드/타입 이름 매핑 예

| `.NET` | Java |
|--------|------|
| `IZLinkChannelClient.RequestToChannel(...)` | `ZLinkClient.requestToChannel(...)` |
| `IZLinkFanoutClient.Publish(...)` | `ZLinkFanoutClient.publish(...)` |
| `IZLinkActorManager.GetOrCreateAsync(...)` | `ZLinkActorManager.getOrCreateAsync(...)` |
| `IZLinkSpotManager.CreateAsync(...)` | `ZLinkSpotManager.createAsync(...)` |
| `IZLinkSpotOutbound` | `ZLinkSpotOutbound` |

### 2.3 annotation 매핑

annotation 이름은 `.NET` 의 `[ZLinkX]` action 이름을 그대로 따른다. `Mapping`
suffix는 붙이지 않는다.

| `.NET` attribute | Java annotation |
|------------------|-----------------|
| `[ZLinkRequest]` | `@ZLinkRequest` |
| `[ZLinkSend]` | `@ZLinkSend` |
| `[ZLinkPublish]` | `@ZLinkPublish` |
| `[ZLinkPacket("name")]` | `@ZLinkPacket("name")` |
| `[ZLinkHandlerGroup("name")]` | `@ZLinkHandlerGroup("name")` |
| `[ZLinkSpotRequest]` | `@ZLinkSpotRequest` |
| `[ZLinkSpotSubscription]` | `@ZLinkSpotSubscription` |
| `[ZLinkSpotActorRequest]` | `@ZLinkSpotActorRequest` |
| `[ZLinkStreamPacket]` | `@ZLinkStreamPacket` |

handler 콜백은 `ZLinkPublishHandler`/`ZLinkPublishContext`/`@ZLinkPublish`
계열로 통일한다. (publish 의미를 `Event` 로 바꾸지 않는다.)

### 2.4 값 타입 매핑

| `.NET` | Java | 규칙 |
|--------|------|------|
| `RoutingId` | `RoutingId`(`bindings/java` 재사용) | transport identity primitive |
| `Message` / `ReadOnlyMemory<byte>` | `Message`(`bindings/java` 재사용) | payload primitive |
| `SendFlags` | `SendFlags` | submit option primitive |
| `ValueTask` / `ValueTask<T>` / `Task<T>` | `CompletionStage<Void>` / `CompletionStage<T>` | async submit 기본 |
| `TimeSpan` | `java.time.Duration` | 기간/주기 |
| `ulong` | `long`(unsigned 의미 주석) 또는 의미상 필요하면 `BigInteger` | 부호 없는 정수 |
| `record` / `readonly record struct` | `record` 또는 불변 class | DTO |
| `enum` | `enum`(wire 값은 코드로 확인) | |

`RoutingId`/`Message`/`SendFlags` 는 backend 가 바뀌어도 같은 의미를 유지하는
허용 primitive다([backend-dependency-policy](./backend-dependency-policy.ko.md) §4).

### 2.5 cancellation 표현

`.NET` 의 `CancellationToken cancellationToken` 파라미터는 Java handler 시그니처에
**별도 파라미터로 옮기지 않는다.** cancellation은 handler/`context` 안으로 접는다.
`ZLinkRequestContext` 등 context가 cancellation 신호(host shutdown, request 취소)를
노출하고, async 반환은 `CompletionStage<T>` 가 책임진다. 이렇게 하면 handler
시그니처가 짧게 유지되고, 취소는 context 한곳에서만 본다.

Kotlin은 Java API의 의미를 바꾸지 않고 아래처럼 감싼다.

```kotlin
suspend fun <TReply : Any> ZLinkClient.request(
    channelName: String,
    request: Any,
    replyType: KClass<TReply>
): TReply
```

## 3. Spring Boot host 매핑

framework는 새 transport를 만들지 않고 기존 binding을 **host 모델로 감싸** 노출한다.
`.NET`은 그 모델이 ASP.NET Core의 DI + hosted service이고(진입점은
`Zlink.Framework.AspNetCore/ServiceCollectionExtensions.cs` 의
`IServiceCollection.AddZLinkFramework(Action<IZLinkFrameworkOptions> configure)`),
Java는 Spring Boot의 bean + auto-configuration starter + lifecycle이다.

### 3.1 등록 진입점

`.NET`의 `IServiceCollection.AddZLinkFramework(options => ...)`는 Java에서
`@EnableZLinkFramework` + auto-configuration starter
(`zlink-framework-spring-boot-starter`)와 `ZLinkFrameworkOptionsCustomizer`로
옮긴다. starter는 framework runtime bean과 lifecycle 구동체를 auto-config로
등록하고, `@EnableZLinkFramework`는 그 auto-config를 켜는 표식이다.

```java
@Configuration
@EnableZLinkFramework
public class ZLinkConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addClientServerChannel("price", channel -> {
            channel.enableServer(server -> server.bind("tcp://0.0.0.0:7301"));
            channel.addRequestHandler(GetPriceHandler.class);
        });
    }
}
```

### 3.2 lifecycle 매핑

Spring lifecycle은 runtime을 새로 정의하지 않고 core runtime을 시작/종료한다.
`.NET` framework runtime은 `IHostedService`(`StartAsync`/`StopAsync`)로
구동·종료된다. Java는 이를 **`SmartLifecycle`** 로 옮긴다. `ApplicationRunner`는
구동 driver로 쓰지 않고, one-shot readiness 신호용으로만 예약한다.

| `.NET`(`IHostedService`) | Spring Boot | 시점 |
|--------------------------|-------------|------|
| `StartAsync` | `SmartLifecycle.start()` | bean 준비 뒤 bind/connect/discovery 시작 |
| `StopAsync` | `SmartLifecycle.stop()` (graceful) | linger/drain graceful close |

`.NET`은 `ZLinkRegistryHostedService` → `ZLinkFrameworkHostedService` →
`ZLinkMonitoringHostedService` 세 hosted service가 같은 순서로 `StartAsync` 된다.
Java는 같은 순서(registry → framework → monitoring)로 구동되도록 `SmartLifecycle`
phase를 정렬한다. framework/registry runtime 구동은 idempotent해야 하며, monitoring
구동체가 같은 runtime을 다시 시동시켜도 두 번 시작되지 않아야 한다. 정식 시작/종료
순서는 [lifecycle-and-failure-semantics](./lifecycle-and-failure-semantics.ko.md)가
소유한다.

handler는 constructor injection을 사용한다. context에 Spring `ApplicationContext`를
넣어 service locator로 쓰지 않는다.

## 4. 등록 표면 대응

| `.NET` builder | Java builder |
|----------------|--------------|
| `AddClientServerChannel` | `addClientServerChannel` |
| `AddFanoutChannel` | `addFanoutChannel` |
| `AddDealerMeshChannel` | `addDealerMeshChannel` |
| `AddRouteMeshChannel` | `addRouteMeshChannel` |
| `AddSpotMesh(...).AddNode(...)` | `addSpotMesh(...).addNode(...)` |
| `AddStreamNode` | `addStreamNode` |
| `UseDiscovery` | `useDiscovery` |
| `UseFilter` | `useFilter` |
| `ConfigureDispatch` | `configureDispatch` |
| `AddActorFactory` | `addActorFactory` |
| `AddSpotRemoteAddressResolver` | `addSpotRemoteAddressResolver` |
| `UseRegistrySpotRemoteAddresses` | `useRegistrySpotRemoteAddresses` |

## 5. packet key와 handler 노출

packet key 해석 순서는 `.NET`과 같다.

1. 호출 또는 등록에서 지정한 `packetName`
2. payload type의 annotation
3. payload type의 `SimpleName`

handler scan은 handler 후보를 찾는 단계다. 실제 channel 노출은 channel builder에
명시적으로 등록된 handler, handler group, annotation binding이 정한다. scan된 모든
handler를 모든 channel에 자동으로 열지 않는다.

## 6. 최종 기준

Java 문서와 `.NET` 문서가 다르면 `.NET` 코드가 기능 기준이다. Java 구현 중 binding
public API가 부족하면 framework 내부에서 reflection이나 internal 접근으로 우회하지
않고 Java binding public API를 추가한다.
