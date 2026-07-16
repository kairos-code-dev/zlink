# .NET 시스템 구조와 host 등록

[.NET 계약 목차](README.ko.md) · [RouteMesh·MeshNode](05-route-mesh.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0을 ASP.NET Core host와 DI에 등록하는 계약을 정의한다. RouteMesh builder,
ChannelName membership, manual peer와 runtime option의 정확한 시그니처는
[05 RouteMesh·MeshNode](05-route-mesh.ko.md)가 소유한다. Handler, context와 messaging client의 시그니처는
[02 handler와 client](02-handler-interfaces.ko.md)가 소유한다.

## 2. Package 경계

| package | 책임 |
|---|---|
| `Zlink.Framework.Contracts` | handler, context, call과 공통 오류 타입 |
| `Zlink.Framework` | RouteMesh, Spot, Actor, STREAM session과 location runtime |
| `Zlink.Framework.AspNetCore` | `IServiceCollection` 등록과 host lifecycle 연결 |
| `Zlink.Framework.Codecs.Protobuf` | 선택 Protobuf codec extension |
| `Zlink.Framework.Codecs.MessagePack` | 선택 MessagePack codec extension |
| `Zlink.Framework.Locations.Redis` | Redis location store extension |

Server framework는 bindings package의 public API만 호출한다. ASP.NET Core adapter는 framework runtime을
DI와 hosted service lifecycle에 연결하며 Core handle을 application에 노출하지 않는다.

## 3. Host 등록

ASP.NET Core 진입점은 다음 시그니처다.

```csharp
public static class ZLinkFrameworkServiceCollectionExtensions
{
    public static IServiceCollection AddZLinkFramework(
        this IServiceCollection services,
        Action<IZLinkFrameworkOptions> configure);
}

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddZLinkMonitoring(
        this IServiceCollection services,
        Action<IZLinkMonitoringOptions> configure);
}
```

한 `IServiceCollection`에 framework root를 한 번 등록한다. `IZLinkFrameworkOptions`의 정확한 멤버는
[05 RouteMesh·MeshNode §2](05-route-mesh.ko.md#2-등록-인터페이스)가 소유한다.

Host는 구성 검증, routing ID 확보, Core handle 생성, bind, peer admission, handler 준비 순서로 시작한다.
Application callback은 handler와 owner queue가 준비된 뒤에만 실행한다. 종료할 때는 새 admission을 막고
request completion, Actor transfer와 STREAM barrier를 deadline까지 진행한 뒤 Core handle을 닫는다.

## 4. DI public service

Framework를 등록하면 다음 service가 public DI surface로 제공된다.

| service | lifetime | 책임 |
|---|---|---|
| `IZLinkRouteClient` | singleton | Node direct와 ChannelName send/request |
| `IZLinkSpotClient` | singleton | resolved Spot direct send/request |
| `IZLinkSpotManager` | singleton | Spot 생성, resolve와 종료 |
| `IZLinkSpotPublisherClient` | singleton | Spot Logical Multicast publish |
| `IZLinkActorClient` | singleton | ActorRef direct send/request |
| `IZLinkActorManager` | singleton | Actor 생성, resolve와 종료 |
| `IZLinkRouteMeshRuntimeOptions` | singleton | MeshNode socket과 ChannelName weight 조회·설정 |
| `IZLinkRouteMeshRuntime` | singleton | MeshNode snapshot, typed event, readiness와 drain |
| `IZLinkMessageFlowRuntime` | singleton | message flow mode와 observer event |
| `IZLinkAllocatedRoutingIdProvider` | singleton | 준비된 allocation 결과 조회 |

등록되지 않은 MeshName, ChannelName 또는 capability를 호출하면 `ZLinkConfigurationException`이 발생한다.
Spot과 Actor handler instance는 owner의 DI scope에서 resolve한다. Handler가 service를 사용할 때는 context를
service locator로 사용하지 않고 constructor injection을 사용한다.

## 5. Redis location store 등록

자동 discovery, 분산 Spot·Actor address 또는 Actor transfer를 사용하는 host는
`Zlink.Framework.Locations.Redis`가 제공하는 store instance를 만든 뒤 root에 명시적으로 등록한다.

```csharp
services.AddZLinkFramework(options =>
{
    options.AddLocationStore(
        new ZLinkRedisLocationStore(redisOptions)); // MeshNode·Spot·Actor location, transfer authority와 lease를 맡는다.
});
```

Redis 전용 registration helper는 제공하지 않는다. Root의 `AddLocationStore(...)`는
`IZLinkLocationStore` instance 하나를 받는다. 같은 instance가 routing ID slot allocation capability도
구현해야 자동 allocation을 사용할 수 있다.

Manual peer만 사용하고 분산 location 기능을 사용하지 않는 MeshNode는 location store 없이 시작할 수 있다.
Manual peer도 MeshName, RID, lifecycle generation, ChannelName set과 security identity admission을 통과한다.

## 6. Codec

Typed handler와 client는 업무 객체를 주고받는다. Framework는 JSON serializer를 기본으로 제공하므로 JSON을
사용하기 위한 message-specific registration API는 없다. Protobuf, MessagePack과 사용자 codec은 root의
codec registry에 extension 단위로 한 번 등록한다.

Codec은 payload와 업무 객체의 변환만 담당한다. Packet name, metadata, routing과 reply correlation은
Framework가 소유한다. Packet name은 handler registration descriptor에서 결정하며 codec을 바꾸어도
dispatch key는 바뀌지 않는다.

## 7. Startup validation

Host는 network bind 전에 다음 조건을 검증한다.

- framework root와 MeshName의 중복
- MeshNode의 routing ID, endpoint와 하나 이상의 ChannelName
- 같은 owner namespace의 handler key 중복
- Spot, Actor와 STREAM factory의 owner 관계
- 자동 discovery 또는 분산 location 기능에 필요한 store instance
- 자동 routing ID와 fixed routing ID의 동시 설정
- TLS certificate, key와 trust 설정

검증 실패는 `ZLinkConfigurationException`으로 host startup을 실패시킨다. Runtime을 first call에서 만들지
않으므로 구성 오류가 message 처리 중에 처음 나타나지 않는다.

## 8. Runtime option

RouteMesh socket과 channel weight의 public runtime option은
[05 RouteMesh·MeshNode §5](05-route-mesh.ko.md#5-publisher와-runtime-option)가 소유한다. 실행 중에는
MeshNode의 `MaxMessageSize`와 ChannelName의 `Weight`를 설정할 수 있다. 다른 transport option은 startup
뒤 setter를 호출하면 `ZLinkConfigurationException`이 발생한다.

Logical Multicast publisher의 `NoDrop` 기본값은 `true`다. 이 설정은 모든 remote pipe와 local Spot queue의
admission을 하나의 publish 결과로 다루게 한다.
