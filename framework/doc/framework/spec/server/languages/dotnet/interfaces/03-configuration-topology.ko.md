# .NET RouteMesh·MeshNode 공개 인터페이스

[.NET exact interface 목차](README.ko.md) · [공통 topology](../../../10-channel-topology.ko.md) ·
[MeshNode](../../../21-mesh-node.ko.md) · [메시지 모델](../../../../03-message-model.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0의 .NET RouteMesh·MeshNode 공개 인터페이스를 고정한다. 대상 독자는
.NET framework와 bindings 구현자다. 물리 mesh 등록, 논리 channel membership, manual peer, handler,
Spot·Actor 등록과 실행 중 weight 변경의 정확한 C# signature를 이 문서가 소유한다.

## 2. 등록 인터페이스

```csharp
public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultRequestTimeout { get; set; }
    TimeSpan ActorTransferForwardWindow { get; set; }
    TimeSpan DefaultSocketSendTimeout { get; set; }
    long ApplicationVersion { get; set; }
    string? MaintenanceWave { get; set; }
    IZLinkCodecRegistryBuilder Codecs { get; }
    IZLinkWorkerOptions Worker { get; }

    void AddHandlersFromAssemblyOf<TMarker>();
    void AddHandlersFromAssemblyOf(Type markerType);
    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);
    void DisableImplicitHandlerAutoRegistration();
    IZLinkMetadataPolicyBuilder ConfigureMetadata();
    void AddLocationStore(IZLinkLocationStore store);
    void AddCheckpointStore(IZLinkCheckpointStore store);
    ZLinkLocationOptions ConfigureLocations();
    IZLinkNetworkOptions ConfigureNetwork();
    IZLinkDispatchOptions ConfigureDispatch();
    IZLinkStreamCompressionBuilder ConfigureStreamCompression();
    void UseFilter<TFilter>() where TFilter : class, IZLinkHandlerFilter;

    IZLinkMeshNodeBuilder AddRouteMesh(string meshName);
    IZLinkClientServerChannelRoleBuilder AddClientServerChannel(string channelName);
    IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName);
    IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName);
}

public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshChannelRoleBuilder Channel(string channelName);
    IZLinkMeshNodeBuilder Listen(string endpoint);
    IZLinkMeshNodeBuilder Listen(int port = 0);
    IZLinkMeshNodeBuilder SetBindHost(string bindHost);
    IZLinkMeshNodeBuilder SetAdvertiseHost(string advertiseHost);
    IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId);
    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkMeshNodeBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkMeshNodeBuilder SetRoutingIdAllocationGroup(string groupName);
    IZLinkMeshNodeSocketConfig ConfigureRouterSocket();
    IZLinkSpotPublisherConfig ConfigureSpotPublisher();
    IZLinkMeshPeerConnections PeerConnections { get; }

    IZLinkMeshNodeBuilder SetDefaultRequestTimeout(TimeSpan timeout);
    IZLinkMeshNodeBuilder AddRouteSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;
    IZLinkMeshNodeBuilder AddRouteSendHandler<THandler>(string? packetName = null)
        where THandler : class;
    IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;
    IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkEntrySpotOptions ConfigureEntrySpot();
    IZLinkMeshNodeBuilder SetEntrySpotRoutingId(RoutingId routingId);
    IZLinkMeshNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;
    IZLinkMeshNodeBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options = null)
        where TSpot : class, IZLinkInstanceSpot;
    IZLinkMeshNodeBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options,
        ZLinkTransferPolicy<TSpot> transfer)
        where TSpot : class, IZLinkInstanceSpot;
    IZLinkMeshNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;
    IZLinkMeshNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;
    IZLinkMeshNodeBuilder AddActorFactory<TActor, TFactory>(
        string actorType,
        ZLinkTransferPolicy<TActor> transfer)
        where TActor : class, IZLinkActor
        where TFactory : class, IZLinkActorFactory<TActor>;
}

public interface IZLinkNetworkOptions
{
    string BindHost { get; set; }
    string? AdvertiseHost { get; set; }
}

public interface IZLinkMeshChannelRoleBuilder
{
    IZLinkMeshChannelClientBuilder Client();
    IZLinkMeshChannelServerBuilder Server();
}

public interface IZLinkMeshChannelClientBuilder
{
}

public interface IZLinkMeshChannelServerBuilder
{
    IZLinkMeshChannelServerBuilder SetWeight(int weight);
    IZLinkMeshChannelServerBuilder AddHandlerGroup(string groupName);
    IZLinkMeshChannelServerBuilder AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;
    IZLinkMeshChannelServerBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;
    IZLinkMeshChannelServerBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;
    IZLinkMeshChannelServerBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkClientServerChannelRoleBuilder
{
    IZLinkClientServerChannelClientBuilder Client();
    IZLinkClientServerChannelServerBuilder Server();
}

public interface IZLinkClientServerChannelClientBuilder
{
    IZLinkClientServerChannelClientBuilder Connect(string endpoint);
}

public interface IZLinkClientServerChannelServerBuilder
{
    IZLinkClientServerChannelServerBuilder Listen(int port = 0);
    IZLinkClientServerChannelServerBuilder SetBindHost(string bindHost);
    IZLinkClientServerChannelServerBuilder SetAdvertiseHost(string advertiseHost);
    IZLinkClientServerChannelServerBuilder SetWeight(int weight);
    IZLinkClientServerChannelServerBuilder AddHandlerGroup(string groupName);
    IZLinkClientServerChannelServerBuilder AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;
    IZLinkClientServerChannelServerBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;
}

public interface IZLinkEndpointConnections
{
    void Connect(string endpoint);
    void Disconnect(string endpoint);
    IReadOnlyList<string> ListConnections();
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder EnablePublisher(string endpoint);
    IZLinkFanoutChannelBuilder EnablePublisher(int port = 0);
    IZLinkFanoutChannelBuilder SetBindHost(string bindHost);
    IZLinkFanoutChannelBuilder SetAdvertiseHost(string advertiseHost);
    IZLinkFanoutChannelBuilder SetRoutingId(RoutingId publisherRoutingId);
    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkFanoutChannelBuilder SetRoutingIdAllocationGroup(string groupName);
    IZLinkFanoutChannelBuilder EnableSubscriber();
    IZLinkFanoutChannelBuilder ConnectSubscriber(string endpoint);
    IZLinkEndpointConnections SubscriberConnections { get; }
    IZLinkFanoutChannelBuilder AddHandler<THandler, TEvent>(
        string? packetName = null)
        where THandler : class, IZLinkFanoutHandler<TEvent>;
}

public interface IZLinkStreamNodeBuilder
{
    IZLinkStreamNodeBuilder Bind(string endpoint);
    IZLinkStreamNodeBuilder Bind(int port = 0);
    IZLinkStreamNodeBuilder SetBindHost(string bindHost);
    IZLinkStreamNodeBuilder SetAdvertiseHost(string advertiseHost);
    IZLinkStreamNodeBuilder EnableActorDispatch(string meshName);
    IZLinkStreamNodeBuilder SetTlsServer(
        string certificatePath,
        string keyPath,
        bool requireClientCertificate = false);
    IZLinkStreamNodeBuilder AddSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkStreamCompressionBuilder
{
    IZLinkStreamCompressionBuilder UseDefault();
    IZLinkStreamCompressionBuilder UseLz4();
    IZLinkStreamCompressionBuilder Use(IZlinkStreamCompressionCodec codec);
    IZLinkStreamCompressionBuilder Disable();
}

public interface IZLinkMetadataPolicyBuilder
{
    IZLinkMetadataPolicyBuilder AllowSessionToActor(string key);
    IZLinkMetadataPolicyBuilder AllowActorToSession(string key);
}

```

`IZLinkCodecRegistryBuilder`와 codec extension의 정확한 선언은
[Serialization](11-serialization.ko.md)이 소유한다.

`AddRouteMesh(meshName)`은 process-local MeshNode 하나를 등록한다. 같은 process에서 같은 `meshName`을
두 번 등록하면 host startup이 `ZLinkConfigurationException`으로 실패한다. `Channel(channelName)` 뒤에는
`Client()` 또는 `Server()`를 정확히 한 번 호출한다. `Client()`는 송신 경로만 만들고, `Server()`만
weight와 handler 등록을 제공한다. Server membership이 없는 MeshNode도 시작할 수 있다.

`Listen(string endpoint)`, `Bind(string endpoint)`와 `EnablePublisher(string endpoint)`를 제공하며,
host·port 조합 overload도 같은 listener 설정을 표현한다.

`AddClientServerChannel(channelName)`도 역할을 정확히 한 번 고른다. Client는 수동 `Connect(...)`가 없으면
location store에서 같은 ChannelName의 server를 자동 발견한다. Server는 받은 send/request handler와 request
reply만 제공하며 연결된 client로 새 업무 호출을 시작하지 않는다.

`ConfigureNetwork()`의 기본 BindHost는 `127.0.0.1`이고 AdvertiseHost를 생략하면 non-wildcard BindHost를
사용한다. Automatic discovery listener는 `Listen()`·`Bind()`·`EnablePublisher()`의 port를 생략하거나
listener 호출 자체를 생략하면 port `0`으로 bind한다. Manual mode에서 endpoint를 다른 discovery source로
얻지 못하면 listen port와 remote endpoint를 명시한다. Listener별 host 설정은 root 기본값보다 우선한다.

Location store를 등록한 fanout publisher는 고정 Publisher RID와 자동 할당 중 하나를 startup 전에
선택하고 전용 descriptor를 게시한다. Store가 없는 publisher는 listener endpoint를 수동으로 전달하는
대상으로 계속 사용할 수 있지만 RID allocation과 automatic discovery 등록은 수행하지 않는다. Endpoint를
받지 않는 `EnableSubscriber()`는 location store에서 같은 ChannelName의 유효한 publisher를 모두 발견한다.
`ConnectSubscriber(endpoint)`는 명시한 endpoint만 사용하는 manual subscriber를 구성한다. 한 fanout
channel에서 automatic subscriber와 manual subscriber를 함께 설정하면 startup이 실패한다. Automatic
subscriber는 location store가 필요하지만 manual publisher와 manual subscriber만 사용하는 host에는
필요하지 않다.

`UseAllocatedRoutingId(...)`의 `slotCount`는 `1..65535`다. 같은 allocation group에 정규화한 MeshNode와
fanout publisher member를 합쳐 `1..255`개만 둘 수 있으며 범위를 벗어나면 startup 설정 오류다.

Framework가 모든 registration에서 만든 fully encoded MeshNode descriptor는 1 MiB 이하여야 한다.
Spot type과 stateful object capability collection은 각각 최대 1024개이고, capability 하나의 readable state
contract ID도 최대 1024개다. Runtime은 완성된 descriptor를 socket bind 전에 한 번에 검증한다. Bound를 넘으면
startup을 실패시키며 collection을 truncate·split하거나 descriptor 일부를 게시하지 않는다.

`SubscriberConnections`는 manual subscriber endpoint 집합의 runtime handle이다. Builder에서 등록한
endpoint와 같은 집합을 대상으로 연결, 해제와 현재 목록 조회를 제공한다. Automatic subscriber의
discovery 결과는 이 handle로 변경하지 않는다.

`AddHandlersFromAssemblyOf(...)`와 `AddHandlersFromAssembly(...)`는 명시한 assembly만 handler scan 범위로
추가한다. Scan에 사용하는 method, group과 packet attribute의 정확한 선언은
[Common runtime](01-common-runtime.ko.md)가 소유한다.

`EnableActorDispatch(meshName)`은 STREAM node가 session Actor dispatch에 사용할 MeshName 하나를 고정한다.
같은 builder에서 두 번 호출하거나 등록되지 않은 MeshName을 지정하면 startup이 실패한다. Actor dispatch를
사용하지 않는 STREAM node는 이 메서드를 호출하지 않아도 된다. Session의 `BindAsync(ActorRef)`는 이
MeshName context를 사용하므로 호출마다 mesh 이름을 받지 않는다.

`DefaultRequestTimeout`의 기본값은 30초, `DefaultSocketSendTimeout`의 기본값은 1초다. `Worker`는 bounded
worker scheduler의 최소·최대 thread 수, idle timeout과 queue 상한을 host startup 전에 설정한다. Raw receive
batch와 service protocol claim 크기는 public 설정으로 노출하지 않는다.

`ConfigureStreamCompression()`과 `IZLinkStreamCompressionBuilder`는 STREAM payload compression을 고른다.
이 builder는 service transport lifecycle이나 checkpoint codec을 설정하지 않는다.

`ApplicationVersion`은 host 전체에 한 번 설정하며 `0..long.MaxValue` 범위이고 기본값은 `0`이다. 모든 local
MeshNode가 이 값을 게시하며 음수는 startup 전에 `ZLinkConfigurationException`으로 거부한다.
`MaintenanceWave`는 `null`이면 wave exclusion을 사용하지 않는 stable ID다.

Policy를 받지 않는 Actor·Instance factory overload는 `ZLinkTransferPolicy<T>.Disabled`와 같다. Typed
overload의 `transfer`가 `null`이면 `ArgumentNullException`으로 거부한다. Actor와 Instance Spot만 typed transfer
policy를 받는다. User Spot용 `AddSpotFactory<TSpot>()`와 Entry Spot 등록에는 transfer parameter를 제공하지
않는다.

Actor transfer policy는 typed factory overload에서 factory와 함께 등록한다. 별도 transfer adapter registry나
operation별 policy 입력은 제공하지 않는다.

## 3. Manual peer

```csharp
public readonly record struct ZLinkMeshPeerConnection(
    string Endpoint,
    RoutingId? ExpectedRoutingId);

public interface IZLinkMeshPeerConnections
{
    void Connect(string endpoint);
    void Connect(RoutingId expectedRoutingId, string endpoint);
    void Disconnect(string endpoint);
    IReadOnlyList<ZLinkMeshPeerConnection> ListConnections();
}
```

`AddInstanceSpotFactory`의 type 이름은 비어 있을 수 없고 UTF-8로 255 byte 이하여야 한다. Option의
`MaxActiveInstances`는 0 sentinel을 사용하지 않으며 option 인자를 생략하면 4096을 사용한다.
`ActivationTimeout`도 0 sentinel을 사용하지 않으며 생략하면 3초를 사용한다.
Option 객체를 제공하면 두 값은 모두 0보다 커야 하며 0을 sentinel이나 무제한으로 해석하지 않는다.
같은 MeshNode에서 같은 stable type 또는 같은 implementation class를
User Spot factory와 Instance factory에 중복 등록할 수 없다. `TSpot`이 닫힌 generic
`IZLinkSpotActorLifecycle<TActor>`도 구현하면
actor-free 계약과 충돌하므로 startup이 실패한다. 두 option은 local MeshNode와 Instance type별로 적용한다.
등록한 type set은 descriptor를 처음 게시하기 전에 고정하며 startup 이후 변경하지 않는다.

expected RID를 생략하면 admission handshake가 remote identity를 결정한다. expected RID를 지정한 경우
handshake identity가 다르면 연결을 admission하지 않는다. Manual 연결도 자동 discovery 연결과 같은
MeshName·RID·ChannelName·security 검증을 사용한다.

## 4. Channel handler scope

Channel handler는 `(ChannelName, message kind, packet name)`으로 구분한다. RID direct route
handler는 MeshNode builder에 등록하며 source RID를 제공하는 route handler context를 사용한다. 같은 key의
중복 등록은 startup 오류이고, 서로 다른 channel이나 route family에 같은 packet name을 등록할 수 있다.

`AddHandlerGroup(groupName)`은 scan으로 찾은 handler 중 같은 `ZLinkHandlerGroupAttribute` 값을 가진
send/request handler를 해당 ChannelName에 노출한다. TicTacToe처럼 수동 등록을 보여 주는 경우에만
typed `AddSendHandler(...)`·`AddRequestHandler(...)`를 직접 사용한다.

`IZLinkMeshChannelServerBuilder`와 `IZLinkClientServerChannelServerBuilder`의 weight는 0부터 100까지다.
0은 해당 channel의 새 select-one과 RouteMesh Logical Multicast remote target에서만
제외한다. RID direct route, 다른 membership과 이미 제출한 operation에는 영향을 주지 않는다.

## 5. Publisher와 runtime option

```csharp
public interface IZLinkSpotPublisherConfig
{
    int SendHighWaterMark { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? Linger { get; set; }
}

public interface IZLinkSpotSubscriberConfig
{
    int ReceiveHighWaterMark { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? Linger { get; set; }
}

public interface IZLinkSocketConfig
{
    long MaxMessageSize { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    int SendBufferSize { get; set; }
    int ReceiveBufferSize { get; set; }
    TimeSpan? Linger { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? ConnectTimeout { get; set; }
    TimeSpan? HandshakeInterval { get; set; }
    bool IPv6 { get; set; }
    bool TcpNoDelay { get; set; }
    bool Immediate { get; set; }
    int Weight { get; set; }
}

public interface IZLinkRouteConfig
{
    bool RequireKnownPeer { get; set; }
    bool AllowPeerHandover { get; set; }
    bool EnablePeerProbe { get; set; }
    RoutingId ConnectRoutingId { get; set; }
}

public interface IZLinkOutboundRouteConfig
{
    bool ProbeRouterOnConnect { get; set; }
}

public interface IZLinkRouteMeshRuntimeOptions
{
    IZLinkMeshChannelRuntimeOptions Channel(string channelName);
}

public interface IZLinkMeshChannelRuntimeOptions
{
    int Weight { get; set; }
}

public interface IZLinkMeshNodeSocketConfig
{
    long MaxMessageSize { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    ulong MailboxMessageBudget { get; set; }
    ulong MailboxByteBudget { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? SendTimeout { get; set; }
}
```

`ConfigureSpotPublisher()`는 publish 전용 전달 정책 option을 제공하지 않는다. Logical Multicast의
publish는 pending queue 없이 bounded I/O executor에 direct handoff한다. 즉시 worker slot을 얻지 못하면 raw
socket call을 시작하지 않고 `Backpressured`를 반환한다. Slot을 얻으면 bindings의 public raw socket call을
정확히 한 번 실행한다. 각 remote
target은 MeshNode ROUTER의 HWM과 send timeout을 따르며, public raw socket call이 target별 deadline까지
수락하지 못한 결과는
`Backpressured`와 partial detail에 보존한다. Remote capacity drop이 없으면 모든 remote route가 준비되지 않은
경우에도 top-level status는 `Submitted`일 수 있다. 앞에서 수락된 target은 뒤 target의 실패 때문에 취소되지
않으며, local Spot queue의 drop은 detail에만 반영하고 top-level status를 바꾸지 않는다.

`IZLinkRouteMeshRuntimeOptions`는 public DI singleton이다. 등록되지 않은 membership을 조회하면
`ZLinkConfigurationException`이다. `MailboxMessageBudget`와 `MailboxByteBudget`은 owner별 application
mailbox의 메시지 수와 byte 수 상한이다. 0은 Framework profile의 유한 기본값을 사용한다. 두 값은
`ConfigureRouterSocket()`에서 startup 전에 설정하며, Logical Multicast의 local target drop도 이 공개
용량 설정을 따른다.

실행 중에는 `Channel(channelName).Weight`만 변경할 수 있다.
ChannelName은 local RouteMesh 또는 ClientServer Server 등록을 유일하게 고른다. HWM과 timeout은
`ConfigureRouterSocket()`에서 startup 전에 설정한다. MeshNode가 지원하지 않는 raw ROUTER option을 이
interface에 노출하지 않는다.

`MaxMessageSize`는 startup 전에만 설정하며 실행 중 setter를 제공하지 않는다. `0`은 bindings 또는
transport가 수신할 수 있는 최대 complete message 크기로 정규화한다. Transport가 unlimited이면 service
wire의 `uint32` 표현 한계에서 envelope overhead를 뺀 값을 사용한다. 양수는 그 표현 한계를 넘을 수 없으며
넘으면 `ZLinkConfigurationException`으로 startup을 실패시킨다. Peer는 정규화한 값을 내부 handshake로
교환하고 sender와 receiver는 두 값 중 작은 effective bound를 complete message allocation 전에 적용한다.
이 negotiation을 위한 public option은 제공하지 않는다.

## 6. 메시징 metadata

Node direct, ChannelName, Spot direct, Actor send/request와 Logical Multicast call builder는 다음 overload를 공통으로 가진다.
handler context는 변경할 수 없는 `ZLinkMessageMetadata` snapshot을 제공한다.

```csharp
public interface IZLinkMetadataCall<TSelf>
{
    TSelf Metadata(string key, string value);
    TSelf Metadata(ZLinkMessageMetadata metadata);
}
```

같은 key를 여러 번 설정하면 마지막 값이 전송된다. metadata 전체의 UTF-8 encoded 크기는 1024 bytes를
넘을 수 없다. reply는 request metadata를 자동 복사하지 않으며 일반 reply에는 metadata setter를 두지
않는다. STREAM session과 Actor relay에 적용할 allowlist는 root `ConfigureMetadata()`가 소유한다.

## 7. Location store와 startup

자동 discovery, 분산 Spot·Actor 주소 또는 Actor transfer를 사용하는 host는 location store를 명시적으로
등록해야 한다. 공식 Redis location store package가 production 기본 구현이다. 등록이 없으면 host startup이
실패한다. process-local in-memory 구현은 단일 process contract test에서만 등록할 수 있다.
정확한 store capability와 Redis 생성자·option은
[.NET Location과 maintenance](08-location-maintenance.ko.md)가 소유한다.
