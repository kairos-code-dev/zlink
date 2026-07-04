namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext : IZLinkSessionContext
{
    private readonly ZLinkSessionActorsContext _actorSurface;
    private readonly ZLinkSessionClientContext _client;
    private readonly Func<ValueTask> _closeAsync;
    private readonly Func<CancellationToken, ValueTask> _closeByProxyAsync;
    private readonly ZLinkSessionRequestTracker _requests = new();
    private readonly IZLinkStream _stream;
    private ZLinkSessionDispatchContext? _currentDispatch;
    private ZLinkSessionStreamTransport? _transport;

    public ZLinkSessionContext(
        ZLinkFrameworkRuntime runtime,
        IZLinkStream stream,
        Func<ValueTask> closeAsync,
        Func<CancellationToken, ValueTask> closeByProxyAsync)
    {
        Runtime = runtime;
        _stream = stream;
        _closeAsync = closeAsync;
        _closeByProxyAsync = closeByProxyAsync;
        ActorCoordinator = new ZLinkSessionActorCoordinator(runtime, stream);
        _client = new ZLinkSessionClientContext(this);
        _actorSurface = new ZLinkSessionActorsContext(this, ActorCoordinator);
    }

    private ZLinkSessionStreamTransport Transport
        => _transport ??= new ZLinkSessionStreamTransport(_stream, _requests, Runtime.Flow);

    internal ZLinkFrameworkRuntime Runtime { get; }

    internal IReadOnlyCollection<IZLinkSessionActor> BoundActors => ActorCoordinator.BoundActors;
    internal ZLinkSessionActorCoordinator ActorCoordinator { get; }

    internal ZLinkCodecRegistryBuilder Codecs => Runtime.Registration.Codecs;
    internal IZlinkStreamCompressionCodec? CompressionCodec => Runtime.Registration.StreamCompressionCodec;

    internal ZlinkStreamHeader? CurrentDispatchHeader => _currentDispatch?.Header;

    public string SessionId => _stream.SessionId;

    public RoutingId? RoutingId => _stream.RoutingId;

    public string? LocalAddr => _stream.LocalAddr;

    public string? RemoteAddr => _stream.RemoteAddr;

    public IZLinkSessionClient Client => _client;

    public IZLinkSessionActors Actors => _actorSurface;

    public ValueTask CloseAsync()
    {
        return _closeAsync();
    }

    internal ValueTask CloseByProxyAsync(CancellationToken cancellationToken = default)
    {
        return _closeByProxyAsync(cancellationToken);
    }

    internal async ValueTask RelayActorRefAsync(
        ZLinkSessionActor actor,
        Message payload,
        CancellationToken cancellationToken)
    {
        var dispatch = _currentDispatch
                       ?? throw new InvalidOperationException(
                           "Session actor relay requires an active stream dispatch.");
        var header = dispatch.Header
                     ?? throw new InvalidOperationException("Session actor relay requires runtime dispatch state.");

        await ActorCoordinator.RelayToActorAsync(
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
        await ActorCoordinator.NotifyActorDisconnectedAsync(actor, cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask CleanupActorBindingsAsync(CancellationToken cancellationToken)
    {
        await ActorCoordinator.CleanupBindingsAsync(this, cancellationToken).ConfigureAwait(false);
    }

    internal async ValueTask CleanupAsync(CancellationToken cancellationToken)
    {
        await ActorCoordinator.CleanupAsync(this, cancellationToken).ConfigureAwait(false);
        if (_stream.RoutingId is { } sessionRid) Runtime.CleanupActorSessionsForSession(sessionRid);
    }

    internal ZLinkSessionDispatchContext EnterDispatch(ZlinkStreamHeader header)
    {
        _currentDispatch = new ZLinkSessionDispatchContext(header);
        return _currentDispatch;
    }

    internal void ExitDispatch()
    {
        _currentDispatch = null;
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

internal sealed class ZLinkSessionClientContext(ZLinkSessionContext context) : IZLinkSessionClient
{
    public IZLinkSessionSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkSessionSendCall<TMessage>(context, message);
    }

    public IZLinkSessionReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkSessionReplyCall<TMessage>(context, message);
    }
}

internal sealed class ZLinkSessionActorsContext(
    ZLinkSessionContext context,
    ZLinkSessionActorCoordinator actors) : IZLinkSessionActors
{
    public IReadOnlyCollection<IZLinkSessionActor> Bound => actors.BoundActors;

    public ValueTask<IZLinkSessionActor> BindAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        return actors.BindActorAsync(context, actor, cancellationToken);
    }

    public ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default)
    {
        return actors.BindActorAsync(context, actor, cancellationToken);
    }

    public ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default)
    {
        return actors.BindOrGetActorAsync(context, actor, cancellationToken);
    }

    public IZLinkSessionActor? Find(string actorId)
    {
        return actors.FindActor(actorId);
    }
}
