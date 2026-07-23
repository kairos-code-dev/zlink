# .NET channel messaging 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Node direct와 ChannelName

Node direct와 ChannelName은 서로 다른 handler family를 사용한다. Node direct context는 source RID를,
ChannelName context는 logical membership을 제공한다.

```csharp
public interface IZLinkHandlerContext
{
    string? ChannelName { get; }
    string PacketName { get; }
    string? ContentType { get; }
    ZLinkMessageMetadata Metadata { get; }
    string? CorrelationId { get; }
    CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkSendContext : IZLinkHandlerContext
{
    public string ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public string? CorrelationId { get; }
    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkRequestContext : IZLinkHandlerContext
{
    public string ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public string? CorrelationId { get; }
    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkRouteSendContext : IZLinkHandlerContext
{
    public string MeshName { get; }
    public string? ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public string? CorrelationId { get; }
    public CancellationToken ConnectionAborted { get; }
    public RoutingId SourceNodeRid { get; }
    public RoutingId TargetNodeRid { get; }
}

public sealed class ZLinkRouteRequestContext : IZLinkHandlerContext
{
    public string MeshName { get; }
    public string? ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public string? CorrelationId { get; }
    public CancellationToken ConnectionAborted { get; }
    public RoutingId SourceNodeRid { get; }
    public RoutingId TargetNodeRid { get; }
}

public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRequestHandler<in TRequest, TResponse>
{
    ValueTask<TResponse> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkRouteSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken);
}
```

`IZLinkSendHandler`와 `IZLinkRequestHandler`는 `Channel(channelName).Server()` 또는
`AddClientServerChannel(channelName).Server()` builder에 등록한다.
`IZLinkRouteSendHandler`와 `IZLinkRouteRequestHandler`는 MeshNode builder에 등록한다. 같은 packet name을
두 family에 등록할 수 있으며 각 family 안의 중복 key는 startup 오류다.

Global DI client의 Node direct operation은 MeshName을 명시한다. Channel operation은 ChannelName 하나로
process-local RouteMesh 또는 ClientServer 송신 경로를 선택한다.

```csharp
public interface IZLinkRouteClient
{
    IZLinkSendCall SendToNode<TMessage>(
        string meshName,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRequestCall RequestToNode<TRequest>(
        string meshName,
        RoutingId targetNodeRid,
        TRequest request);

    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
        TRequest request);
}
```

Node direct는 target RID 하나로 submit한다. Channel operation은 process-local route index에서 ChannelName으로
유일한 RouteMesh 또는 ClientServer 송신 경로를 선택한다. Ready positive-weight member 하나를
round-robin으로 선택하고 같은 operation에서 submit하며 client는 선택된 RID를 반환하지 않는다.

`IZLinkHandlerContext`는 nullable ChannelName을 제공한다. Channel context는 non-null ChannelName을 제공하고,
Node direct 전용 context만 MeshName과 source·target RID를 추가로 제공한다. Correlation ID는 request에서 non-null이고
send에서 null이며 Framework가 reply route와 함께 보존한다.

Classic fanout handler는 독립 fanout channel에서 받은 typed event만 처리한다.

```csharp
public interface IZLinkFanoutClient
{
    IZLinkFanoutPublishCall Publish<TEvent>(
        string channelName,
        TEvent message);

    IZLinkFanoutPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkFanoutHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        CancellationToken cancellationToken);
}
```

`IZLinkFanoutClient.Publish(...)`는 ChannelName과 typed event를 받고, 명시적인 topic이 필요한 호출은 topic
overload를 사용한다. Topic을 생략하면 Framework가 event의 packet name을 topic으로 사용한다. 반환한 전용
call을 만들 때 topic이 내부 liveness용 exact byte `01 5A 4C 46 31`이면 transport를 시작하지 않고
`ArgumentException`을 발생시킨다. 반환한 전용
call은 local publisher transport의 bounded admission만 `ZLinkSubmitResult`로 알린다. `IZLinkPublishCall`과
`ZLinkPublishResult`는 Logical Multicast의 target별 집계를 위한 별도 계약이며 classic fanout에 사용하지
않는다.
