
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream,
    Func<CancellationToken, ValueTask> closeAsync,
    Func<CancellationToken, ValueTask> closeByProxyAsync)
    : IZLinkSessionContext
{
    private ZlinkStreamHeader? _currentDispatchHeader;
    private readonly ZLinkSessionRequestTracker _requests = new();
    private ZLinkSessionStreamTransport? _transport;
    private readonly ZLinkSessionActorCoordinator _actors = new(runtime, stream);

    private ZLinkSessionStreamTransport Transport
        => _transport ??= new ZLinkSessionStreamTransport(stream, _requests);

    public string SessionId => stream.SessionId;

    public RoutingId? RoutingId => stream.RoutingId;

    public string? LocalAddr => stream.LocalAddr;

    public string? RemoteAddr => stream.RemoteAddr;

    public IReadOnlyCollection<IZLinkSessionActor> BoundActors => _actors.BoundActors;

    public IZLinkSessionSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkSessionSendCall<TMessage>(this, message);
    }

    public IZLinkSessionReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkSessionReplyCall<TMessage>(this, message);
    }

    public ValueTask<IZLinkSessionActor> BindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return _actors.BindActorAsync(this, actorId, cancellationToken);
    }

    public ValueTask<IZLinkSessionActor> BindActorAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default)
    {
        return _actors.BindActorAsync(this, actor, cancellationToken);
    }

    public ValueTask<IZLinkSessionActor> BindActorAsync(
        IZLinkSessionActor actor,
        CancellationToken cancellationToken = default)
    {
        return _actors.BindActorAsync(this, actor, cancellationToken);
    }

    public IZLinkSessionActor? FindActor(string actorId)
    {
        return _actors.FindActor(actorId);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        return closeAsync(cancellationToken);
    }

    internal ValueTask CloseByProxyAsync(CancellationToken cancellationToken = default)
    {
        return closeByProxyAsync(cancellationToken);
    }

    internal async ValueTask RelayActorRefAsync(
        ZLinkSessionActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        await _actors.RelayToActorAsync(
                actor,
                header,
                payload,
                ReplyActorRawAsync,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorRefDisconnectedAsync(
        ZLinkSessionActor actor,
        CancellationToken cancellationToken)
    {
        await _actors.NotifyActorDisconnectedAsync(actor, cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZlinkStreamHeader? CurrentDispatchHeader => _currentDispatchHeader;

    internal async ValueTask CleanupActorBindingsAsync(CancellationToken cancellationToken)
    {
        await _actors.CleanupBindingsAsync(this, cancellationToken).ConfigureAwait(false);
    }

    internal async ValueTask CleanupAsync(CancellationToken cancellationToken)
    {
        await _actors.CleanupAsync(this, cancellationToken).ConfigureAwait(false);
        if (stream.RoutingId is { } sessionRid)
        {
            runtime.CleanupActorSessionsForSession(sessionRid);
        }
    }

    internal void EnterDispatch(ZlinkStreamHeader header)
    {
        _currentDispatchHeader = header;
    }

    internal void ExitDispatch()
    {
        _currentDispatchHeader = null;
    }

    internal bool TryCompleteResponse(
        ZlinkStreamHeader header,
        Message payload)
    {
        return _requests.TryCompleteResponse(header, payload);
    }

    internal bool Write(Message payload)
    {
        return Transport.Write(payload);
    }

    internal ValueTask SendRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        return Transport.SendRawAsync(packetName, codec, payload, cancellationToken);
    }

    internal async ValueTask<Message> RequestRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await Transport.RequestRawAsync(packetName, codec, payload, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        return Transport.ReplyRawAsync(requestHeader, codec, payload, cancellationToken);
    }

    internal ValueTask ReplyActorRawAsync(
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply,
        CancellationToken cancellationToken)
    {
        return Transport.ReplyRawAsync(requestHeader, reply, cancellationToken);
    }

    internal ValueTask ReplyErrorAsync(
        ZlinkStreamHeader requestHeader,
        Exception exception,
        CancellationToken cancellationToken)
    {
        return Transport.ReplyErrorAsync(requestHeader, exception, cancellationToken);
    }

}
