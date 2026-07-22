# .NET STREAM server session 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. STREAM server session

STREAM session은 lifecycle과 typed packet handler를 소유한다. Transport callback은 application callback을
직접 실행하지 않는다.

```csharp
public interface IZLinkSession
{
    IZLinkSessionContext Context { get; }
    void Configure() { }
    ValueTask OnConnectedAsync(CancellationToken cancellationToken);
    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);
    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);
    ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSessionContext
{
    string SessionId { get; }
    RoutingId? RoutingId { get; }
    string? LocalAddr { get; }
    string? RemoteAddr { get; }
    IZLinkSessionClient Client { get; }
    IZLinkSessionActors Actors { get; }
    IZLinkSessionHandlerRegistry Handlers { get; }
    ValueTask CloseAsync();
}

public interface IZLinkSessionHandlerRegistry
{
    void AddHandler<THandler>() where THandler : class;
    void AddHandler<THandler>(string packetName) where THandler : class;
    ValueTask<bool> TryHandleAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionPacketHandler<in TSessionContext, TMessage>
{
    ValueTask HandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionClient
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);
    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionSendCall
    : IZLinkMetadataCall<IZLinkSessionSendCall>
{
    IZLinkSessionSendCall Compress();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Compress();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }
    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
    ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
    IZLinkSessionActor? Find(string actorId);
}

public interface IZLinkSessionActor
{
    string ActorId => Ref.ActorId;
    ActorRef Ref { get; }
    ValueTask<ZLinkSubmitResult> RelayAsync(
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkSubmitResult> RelayAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}

public enum ZLinkStreamSessionError
{
    Internal = 0,
    TransportError = 1
}

public readonly record struct ZLinkStreamDiagnostic(
    int NativeCode,
    string? Message);

public readonly record struct ZLinkStreamError(
    ZLinkStreamSessionError Error,
    ZLinkStreamDiagnostic? Diagnostic);

public sealed class ZLinkSessionDispatchContext
{
    public ZLinkSessionDispatchContext(
        string packetName,
        ZLinkMessageMetadata? metadata = null,
        bool canReply = false) { }
    public string PacketName { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public bool CanReply { get; }
}
```

`IZLinkSessionReplyCall`은 현재 request sequence와 one-shot reply token을 전송 전에 검증한다. 유효한 첫
terminator는 transport를 시작하기 전에 token을 원자적으로 claim하고 소비한다. 같은 token에서 만든 두 call이
경쟁하면 claim에 실패한 call은 transport를 시도하지 않고 exceptional completion으로 끝난다. Send packet에서
만든 reply, 이미 사용한 token과 중복 submit도 같은 방식으로 거부한다. Token을 소비한 call이 timeout,
`Backpressured` 또는 cancellation로 끝나도 token을 다시 사용할 수 없다. 유효한 reply는 STREAM socket send
timeout만 admission deadline으로 사용한다. Caller request timeout은 wire로 전달되지 않으므로 reply deadline으로
사용하지 않으며, timeout이나 cancellation 뒤에는 late reply를 보내지 않는다.

Payload만 받는 `RelayAsync(...)`는 local relay queue가 operation을 수락했는지만
`ZLinkSubmitResult`로 반환하는 one-way admission이다. Dispatch context를 받는 overload는 explicit
current STREAM request reply capability를 호출 즉시 runtime에 이전한다. Submitted면 Actor typed reply가
original STREAM correlation을 terminal-once로 완료하고 admission failure면 Framework가 같은 correlation을
typed failure로 완료한다. Caller는 별도 reply·retry를 하지 않는다. One-way dispatch context는 reply
capability가 없으므로 admission만 반환한다.

같은 session의 packet과 lifecycle callback은 직렬로 실행한다. Handshake와 node 범위 오류는 runtime
monitoring으로 보고하며 `OnErrorAsync(...)`에 전달하지 않는다.

Session binding은 `ActorRef.ActorId + ObjectGeneration`의 exact incarnation을 한 번 고정한다. Ref의
MeshName·NodeRid는 최초 control route snapshot으로 사용한다. Mapping이 없으면 `ActorLocationStale`, current
generation이 다르면 `ActorGenerationStale`, pre-commit seal 중이면 `ActorMoving`이다. Framework는 Store에서
다른 ref를 찾아 같은 bind operation을 hidden retry하지 않는다. Bind 뒤 Actor transfer가 commit되면 runtime이
binding route를 갱신한다. Local `IZLinkActor`를 받는 overload는 제공하지 않는다.

## 2. STREAM transport handle

```csharp
public interface IZLinkStream
{
    string SessionId { get; }
    RoutingId? RoutingId { get; }
    string? LocalAddr { get; }
    string? RemoteAddr { get; }
    bool Write(
        ZLinkMessage payload,
        SendFlags flags = SendFlags.None);
    ValueTask CloseAsync();
}

public interface IZLinkMessageMetadataPolicy
{
    bool CanForward(string key);
}
```

`IZLinkStream`은 session callback에서 transport-facing operation을 제공한다. Bound session과 typed call은
이 interface와 별도 책임을 가진다.
