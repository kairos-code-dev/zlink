
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkClient client,
    IZLinkStream stream,
    IZlinkStreamHeaderCodec headerCodec,
    Func<CancellationToken, ValueTask> closeAsync,
    Func<CancellationToken, ValueTask> closeByProxyAsync)
    : IZLinkSessionContext, IZLinkSessionActorAttachmentContext
{
    private IZLinkActor? _actor;
    private ZlinkStreamHeader? _currentDispatchHeader;
    private readonly ZLinkSessionRequestTracker _requests = new();
    private ZLinkSessionStreamTransport? _transport;
    private readonly ZLinkSessionActorBindingRegistry _actorBindings = new(runtime);
    private readonly ZLinkSessionActorRelay _actorRelay = new(runtime);

    private ZLinkSessionStreamTransport Transport
        => _transport ??= new ZLinkSessionStreamTransport(stream, _requests);

    public string SessionId => stream.SessionId;

    public RoutingId? RoutingId => stream.RoutingId;

    public string? LocalAddr => stream.LocalAddr;

    public string? RemoteAddr => stream.RemoteAddr;

    public IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request)
    {
        return client.Request(channelName, request);
    }

    public IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message)
    {
        return client.Send(channelName, message);
    }

    public IZLinkSessionSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkSessionSendCall<TMessage>(this, message);
    }

    public IZLinkSessionReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkSessionReplyCall<TMessage>(this, message);
    }

    public async ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var route = await ResolveActorRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        var isLocalRoute = route.TargetNodeRid.Equals(runtime.ResolveSessionRouterId(route.RouterChannelId));
        if (isLocalRoute)
        {
            EnsureLocalActorExists(actorId, actorType);
        }

        Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync =
            isLocalRoute
            ? NotifyLocalActorDisconnectedAsync
            : NotifyRoutedActorDisconnectedAsync;
        return await _actorBindings.BindAsync(
            this,
            SessionId,
            actorId,
            actorType,
            route.RouterChannelId,
            route.TargetNodeRid,
            notifyDisconnectedAsync,
            cancellationToken).ConfigureAwait(false);
    }

    private void EnsureLocalActorExists(string actorId, string actorType)
    {
        if (runtime.TryGetCreatedActor(actorId, actorType, out _))
        {
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor '{actorId}' is not created on the local actor runtime.");
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
        ArgumentNullException.ThrowIfNull(actor);
        await runtime.AttachActorAsync(actor, stream, cancellationToken);
        _actor = actor;
    }

    public async ValueTask RelayToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        if (actor is not ZLinkActorRef actorRef)
        {
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");
        }

        if (IsLocalActorRoute(actorRef))
        {
            using (payload)
            {
                await runtime.SubmitActorByIdAsync(
                        actorRef.ActorId,
                        header,
                        payload,
                        cancellationToken)
                    .ConfigureAwait(false);
            }

            return;
        }

        await _actorRelay.DispatchRemoteAsync(
                actorRef,
                header,
                payload,
                ReplyRawAsync,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private bool IsLocalActorRoute(ZLinkActorRef actor)
    {
        return actor.TargetNodeRid.Equals(runtime.ResolveSessionRouterId(actor.RouterChannelId));
    }

    private async ValueTask DisconnectActorAsync(
        CancellationToken cancellationToken = default)
    {
        var actor = _actor;
        if (actor is null)
        {
            return;
        }

        await runtime.DisconnectActorAsync(actor, stream, cancellationToken);
        _actor = null;
    }

    internal ZlinkStreamHeader? CurrentDispatchHeader => _currentDispatchHeader;

    internal IZlinkStreamHeaderCodec HeaderCodec => headerCodec;

    internal async ValueTask CleanupActorBindingsAsync(CancellationToken cancellationToken)
    {
        await _actorBindings.CleanupAsync(this, cancellationToken).ConfigureAwait(false);
    }

    internal async ValueTask CleanupAsync(CancellationToken cancellationToken)
    {
        await DisconnectActorAsync(cancellationToken).ConfigureAwait(false);
        await CleanupActorBindingsAsync(cancellationToken).ConfigureAwait(false);
    }

    internal void EnterDispatch(ZlinkStreamHeader header)
    {
        _currentDispatchHeader = header;
    }

    internal void ExitDispatch()
    {
        _currentDispatchHeader = null;
    }

    private async ValueTask<ZLinkActorRoute> ResolveActorRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        if (runtime.Services.GetService(typeof(IZLinkActorPlayRouteResolver))
            is IZLinkActorPlayRouteResolver resolver)
        {
            return await resolver.ResolvePlayRouteAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }

        return runtime.ResolveLocalActorRoute();
    }

    private async ValueTask NotifyLocalActorDisconnectedAsync(
        ZLinkActorRef actor,
        CancellationToken cancellationToken)
    {
        await runtime.NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask NotifyRoutedActorDisconnectedAsync(
        ZLinkActorRef actor,
        CancellationToken cancellationToken)
    {
        await _actorRelay.NotifyDisconnectedAsync(actor, cancellationToken)
            .ConfigureAwait(false);
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
