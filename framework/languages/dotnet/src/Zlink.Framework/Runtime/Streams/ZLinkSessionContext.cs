
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext : IZLinkSessionContext
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly IZLinkStream _stream;
    private readonly Func<ValueTask> _closeAsync;
    private readonly Func<CancellationToken, ValueTask> _closeByProxyAsync;
    private ZLinkSessionDispatchContext? _currentDispatch;
    private readonly ZLinkSessionRequestTracker _requests = new();
    private ZLinkSessionStreamTransport? _transport;
    private readonly ZLinkSessionActorCoordinator _actors;
    private readonly ZLinkSessionClientContext _client;
    private readonly ZLinkSessionActorsContext _actorSurface;

    public ZLinkSessionContext(
        ZLinkFrameworkRuntime runtime,
        IZLinkStream stream,
        Func<ValueTask> closeAsync,
        Func<CancellationToken, ValueTask> closeByProxyAsync)
    {
        _runtime = runtime;
        _stream = stream;
        _closeAsync = closeAsync;
        _closeByProxyAsync = closeByProxyAsync;
        _actors = new ZLinkSessionActorCoordinator(runtime, stream);
        _client = new ZLinkSessionClientContext(this);
        _actorSurface = new ZLinkSessionActorsContext(this, _actors);
    }

    private ZLinkSessionStreamTransport Transport
        => _transport ??= new ZLinkSessionStreamTransport(_stream, _requests, _runtime.Flow);

    internal ZLinkFrameworkRuntime Runtime => _runtime;

    public string SessionId => _stream.SessionId;

    public RoutingId? RoutingId => _stream.RoutingId;

    public string? LocalAddr => _stream.LocalAddr;

    public string? RemoteAddr => _stream.RemoteAddr;

    public IZLinkSessionClient Client => _client;

    public IZLinkSessionActors Actors => _actorSurface;
    internal IReadOnlyCollection<IZLinkSessionActor> BoundActors => _actors.BoundActors;
    internal ZLinkSessionActorCoordinator ActorCoordinator => _actors;
    internal ZLinkCodecRegistryBuilder Codecs => _runtime.Registration.Codecs;

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
            ?? throw new InvalidOperationException("Session actor relay requires an active stream dispatch.");
        var header = dispatch.Header
            ?? throw new InvalidOperationException("Session actor relay requires runtime dispatch state.");

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

    internal ZlinkStreamHeader? CurrentDispatchHeader => _currentDispatch?.Header;

    internal async ValueTask CleanupActorBindingsAsync(CancellationToken cancellationToken)
    {
        await _actors.CleanupBindingsAsync(this, cancellationToken).ConfigureAwait(false);
    }

    internal async ValueTask CleanupAsync(CancellationToken cancellationToken)
    {
        await _actors.CleanupAsync(this, cancellationToken).ConfigureAwait(false);
        if (_stream.RoutingId is { } sessionRid)
        {
            _runtime.CleanupActorSessionsForSession(sessionRid);
        }
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

    public IZLinkSessionActor? Find(string actorId)
    {
        return actors.FindActor(actorId);
    }
}
