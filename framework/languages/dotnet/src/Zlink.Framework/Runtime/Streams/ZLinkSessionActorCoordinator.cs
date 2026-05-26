namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings = new(runtime);
    private IZLinkActor? _attachedActor;

    public IReadOnlyCollection<IZLinkSessionActor> BoundActors => _bindings.BoundActors;

    public async ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        string actorId,
        CancellationToken cancellationToken)
    {
        var actorRef = ResolveActorRefForBinding(actorId);
        await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
        return await _bindings.BindAsync(
            context,
            ToPublicActorRef(actorRef),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        return await BindActorCoreAsync(
                context,
                actor,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<IZLinkSessionActor> BindActorCoreAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        EnsureConcreteActorRef(actor);
        var actorRef = new ZLinkBackendActorRef(
            actor.NodeRid,
            actor.ActorId,
            actor.Generation);
        await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
        return await _bindings.BindAsync(
            context,
            actor,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        IZLinkSessionActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return await BindActorCoreAsync(
                context,
                actor.Ref,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public IZLinkSessionActor? FindActor(string actorId)
    {
        return _bindings.FindActor(actorId);
    }

    public async ValueTask AttachAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        await runtime.AttachActorAsync(actor, stream, cancellationToken).ConfigureAwait(false);
        _attachedActor = actor;
    }

    public async ValueTask RelayToActorAsync(
        IZLinkSessionActor actor,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZlinkStreamCodec, ReadOnlyMemory<byte>, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkSessionActor actorRef)
        {
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");
        }

        if (stream is ZLinkManagedStream managedStream)
        {
            await SendBoundActorAsync(managedStream, actorRef, header, payload, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await DispatchLocalAsync(actorRef, header, payload, replyRawAsync, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyActorDisconnectedAsync(
        IZLinkSessionActor actor,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkSessionActor actorRef)
        {
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");
        }

        await runtime.NotifyActorDisconnectedByIdAsync(actorRef.ActorId, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask CleanupBindingsAsync(
        ZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        await _bindings.CleanupAsync(context, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask CleanupAsync(
        ZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        await DisconnectAttachedActorAsync(cancellationToken).ConfigureAwait(false);
        await CleanupBindingsAsync(context, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchLocalAsync(
        ZLinkSessionActor actorRef,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZlinkStreamCodec, ReadOnlyMemory<byte>, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        using var actorPayload = payload.Copy();
        if (header.RequestSeq is not null)
        {
            var reply = await runtime.SubmitActorForReplyAsync(
                    actorRef.ActorId,
                    header,
                    actorPayload,
                    cancellationToken)
                .ConfigureAwait(false);
            await replyRawAsync(header, header.Codec, reply, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await runtime.SubmitActorByIdAsync(
                actorRef.ActorId,
                header,
                actorPayload,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private ZLinkBackendActorRef ResolveActorRefForBinding(string actorId)
    {
        if (runtime.TryGetCreatedActorState(actorId, out var state))
        {
            return state.NativeActorRef
                ?? throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' does not have a native Actor ref.");
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor '{actorId}' is not created on the local actor runtime.");
    }

    private static ActorRef ToPublicActorRef(ZLinkBackendActorRef actorRef)
        => new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);

    private static void EnsureConcreteActorRef(ActorRef actor)
    {
        if (actor.NodeRid.IsEmpty || actor.Generation == 0)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor ref requires a target SpotNode routing id and concrete actor generation.",
                isRetriable: false);
        }
    }

    private async ValueTask BindNativeActorAsync(
        ZLinkBackendActorRef actorRef,
        CancellationToken cancellationToken)
    {
        if (stream is not ZLinkManagedStream managedStream)
        {
            return;
        }

        await managedStream.BindActorAsync(
                actorRef,
                runtime.Registration.DefaultTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DisconnectAttachedActorAsync(CancellationToken cancellationToken)
    {
        var actor = _attachedActor;
        if (actor is null)
        {
            return;
        }

        await runtime.DisconnectActorAsync(actor, stream, cancellationToken).ConfigureAwait(false);
        _attachedActor = null;
    }

    private async ValueTask SendBoundActorAsync(
        ZLinkManagedStream managedStream,
        ZLinkSessionActor actorRef,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        var bodyBytes = payload.ToArray();

        var timeout = runtime.Registration.DefaultTimeout;
        var retryDelay = TimeSpan.FromMilliseconds(25);
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        ZlinkException? lastError = null;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            using var headerPart = Message.FromBytes(headerBytes);
            using var bodyPart = Message.FromBytes(bodyBytes);
            try
            {
                if (managedStream.SendBoundActor(
                        actorRef.ActorId,
                        new[] { headerPart, bodyPart },
                        SendFlags.None))
                {
                    return;
                }
            }
            catch (ZlinkSubmitException error) when (IsActorGatewayRoutePending(error))
            {
                lastError = error;
            }

            if (elapsed.Elapsed >= timeout)
            {
                throw new InvalidOperationException(
                    "Actor session relay failed because the ActorGateway route was not ready before timeout.",
                    lastError);
            }

            var remaining = timeout - elapsed.Elapsed;
            var delay = remaining < retryDelay ? remaining : retryDelay;
            await Task.Delay(delay, cancellationToken).ConfigureAwait(false);
        }
    }

    private static bool IsActorGatewayRoutePending(ZlinkSubmitException error)
    {
        return error.Result == ZlinkSubmitException.ErrorCode.NotConnected;
    }
}
