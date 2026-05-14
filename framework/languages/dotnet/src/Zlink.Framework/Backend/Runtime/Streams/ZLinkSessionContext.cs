using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkClient client,
    IZLinkStream stream,
    Func<CancellationToken, ValueTask> closeAsync)
    : IZLinkSessionContext, IZLinkSessionActorAttachmentContext
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

    private IZLinkActor? _actor;
    private ZlinkStreamHeader? _currentDispatchHeader;
    private readonly ZLinkSessionRequestTracker _requests = new();
    private ZLinkSessionStreamTransport? _transport;
    private readonly ZLinkSessionActorBindingRegistry _actorBindings = new(runtime);

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

    public async ValueTask<IZLinkActorRef> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var route = runtime.ResolveLocalActorRoute();
        await runtime.CreateLocalActorAsync(actorId, actorType, cancellationToken)
            .ConfigureAwait(false);
        return await _actorBindings.BindAsync(
            this,
            SessionId,
            actorId,
            actorType,
            route.RouterChannelId,
            route.TargetNodeRid,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var route = await ResolveActorRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        return await _actorBindings.BindAsync(
            this,
            SessionId,
            actorId,
            actorType,
            route.RouterChannelId,
            route.TargetNodeRid,
            cancellationToken).ConfigureAwait(false);
    }

    public IZLinkSessionRequestCall Request<TRequest>(TRequest request)
    {
        return new ZLinkSessionRequestCall<TRequest>(
            this,
            request,
            runtime.Registration.DefaultTimeout);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        return closeAsync(cancellationToken);
    }

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        await runtime.AttachActorAsync(actor, stream, cancellationToken);
        _actor = actor;
    }

    public ValueTask DispatchToActorAsync(
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken = default)
    {
        return DispatchToActorAsync(packet.Header, packet.Body.Move(), cancellationToken);
    }

    public ValueTask DispatchToActorAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var actor = _actor
            ?? throw new InvalidOperationException("No actor is attached to the current session.");

        var node = runtime.GetActorSpotNode();
        var actorState = runtime.GetOrCreateActorState(actor.ActorId);
        if (node is not null
            && actorState.NativeActorRef is not null
            && stream is ZLinkManagedStream managedStream)
        {
            using var encodedHeader = Message.FromBytes(HeaderCodec.Encode(header).Span);
            using (body)
            {
                managedStream.SendBoundActor(actor.ActorId, [encodedHeader, body]);
            }
            return ValueTask.CompletedTask;
        }

        return runtime.SubmitActorAsync(actor, header, body, cancellationToken);
    }

    public async ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken = default)
    {
        await DispatchToActorAsync(actor, packet.Header, packet.Body.Move(), cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        if (actor is not ZLinkActorRef actorRef)
        {
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");
        }

        var routeClient = runtime.RouteClient as IZLinkMultipartRouteClient
            ?? throw new InvalidOperationException("Route client does not support multipart internal packets.");

        using (body)
        {
            var parts = ZLinkInternalMultipartPackets.CreateActorDispatchParts(
                actorRef.ActorId,
                actorRef.ActorType,
                header,
                body.AsReadOnlySpan());

            if (header.RequestSeq is not null)
            {
                byte[] reply;
                try
                {
                    reply = await routeClient.RequestPartsTo<byte[]>(
                            actorRef.RouterChannelId,
                            actorRef.TargetNodeRid,
                            ZLinkInternalPacketNames.ActorDispatch,
                            parts,
                            runtime.Registration.DefaultTimeout,
                            cancellationToken)
                        .ConfigureAwait(false);
                }
                catch (TimeoutException ex)
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorDispatchTimeout,
                        $"Actor dispatch request for '{actorRef.ActorId}' timed out.",
                        innerException: ex);
                }
                catch (ZLinkFrameworkException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorDispatchHandlerFailed,
                        $"Actor dispatch request for '{actorRef.ActorId}' failed: {ex.Message}",
                        innerException: ex);
                }

                await ReplyRawAsync(header, header.Codec, reply, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await routeClient.SendPartsTo(
                    actorRef.RouterChannelId,
                    actorRef.TargetNodeRid,
                    ZLinkInternalPacketNames.ActorDispatch,
                    parts,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask DisconnectActorAsync(
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

    internal bool TryCompleteResponse(
        ZlinkStreamHeader header,
        Message body)
    {
        return _requests.TryCompleteResponse(header, body);
    }

    internal bool Write(Message payload)
    {
        return Transport.Write(payload);
    }

    internal ValueTask SendRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        return Transport.SendRawAsync(packetName, codec, body, cancellationToken);
    }

    internal async ValueTask<Message> RequestRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await Transport.RequestRawAsync(packetName, codec, body, timeout, cancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        return Transport.ReplyRawAsync(requestHeader, codec, body, cancellationToken);
    }

    internal ValueTask ReplyErrorAsync(
        ZlinkStreamHeader requestHeader,
        Exception exception,
        CancellationToken cancellationToken)
    {
        return Transport.ReplyErrorAsync(requestHeader, exception, cancellationToken);
    }

    internal async ValueTask<TReply> RequestClientAsync<TRequest, TReply>(
        TRequest request,
        string packetName,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await Transport.RequestJsonAsync<TRequest, TReply>(request, packetName, timeout, cancellationToken)
            .ConfigureAwait(false);
    }
}
