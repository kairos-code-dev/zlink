
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream,
    Func<CancellationToken, ValueTask> closeAsync,
    Func<CancellationToken, ValueTask> closeByProxyAsync)
    : IZLinkSessionContext, IZLinkSessionActorAttachmentContext
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

    public IZLinkSessionSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkSessionSendCall<TMessage>(this, message);
    }

    public IZLinkSessionReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkSessionReplyCall<TMessage>(this, message);
    }

    public ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return _actors.BindHandleAsync(this, actorId, actorType, cancellationToken);
    }

    public ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        ZLinkActorRemoteAddress remoteAddress,
        CancellationToken cancellationToken = default)
    {
        return _actors.BindHandleAsync(this, actorId, actorType, remoteAddress, cancellationToken);
    }

    public ValueTask<IZLinkActorRef> BindActorHandleAsync(
        IZLinkActorRef actor,
        CancellationToken cancellationToken = default)
    {
        return _actors.BindHandleAsync(this, actor, cancellationToken);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        return closeAsync(cancellationToken);
    }

    internal ValueTask CloseByProxyAsync(CancellationToken cancellationToken = default)
    {
        return closeByProxyAsync(cancellationToken);
    }

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await _actors.AttachAsync(actor, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask RelayToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await _actors.RelayToActorAsync(
                actor,
                header,
                payload,
                ReplyRawAsync,
                cancellationToken)
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

    internal ValueTask ReplyErrorAsync(
        ZlinkStreamHeader requestHeader,
        Exception exception,
        CancellationToken cancellationToken)
    {
        return Transport.ReplyErrorAsync(requestHeader, exception, cancellationToken);
    }

}
