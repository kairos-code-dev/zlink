namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings = new(runtime);
    private IZLinkActor? _attachedActor;

    public IReadOnlyCollection<IZLinkActorRef> BoundActors => _bindings.BoundActors;

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        string actorId,
        string actorType,
        CancellationToken cancellationToken)
    {
        var actorRef = ResolveActorRefForBinding(actorId, actorType);
        await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
        return await _bindings.BindAsync(
            context,
            actorType,
            ToPublicActorRef(actorRef),
            ToRemoteAddress(actorRef),
            isRemote: false,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        string actorId,
        string actorType,
        ZLinkActorRemoteAddress remoteAddress,
        CancellationToken cancellationToken)
    {
        EnsureConcreteRemoteAddress(remoteAddress);
        return await BindHandleAsync(
                context,
                new ActorRef(
                    remoteAddress.TargetNodeRid,
                    actorId,
                    remoteAddress.ActorGeneration),
                actorType,
                isRemote: true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        string actorType,
        CancellationToken cancellationToken)
    {
        return await BindHandleAsync(
                context,
                actor,
                actorType,
                isRemote: true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        string actorType,
        bool isRemote,
        CancellationToken cancellationToken)
    {
        EnsureConcreteActorRef(actor);
        var actorRef = new ZLinkBackendActorRef(
            actor.NodeRid,
            actor.ActorId,
            actor.Generation);
        var remoteAddress = ToRemoteAddress(actorRef);
        await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
        return await _bindings.BindAsync(
            context,
            actorType,
            actor,
            remoteAddress,
            isRemote,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        IZLinkActorRef actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        if (actor.IsRemote)
        {
            return await BindHandleAsync(
                    context,
                    actor.Actor,
                    actor.ActorType,
                    isRemote: true,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return await BindHandleAsync(
                context,
                actor.ActorId,
                actor.ActorType,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public bool TryGetBoundActor(
        string actorId,
        out IZLinkActorRef actor)
    {
        return _bindings.TryGetBoundActor(actorId, out actor);
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
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZlinkStreamCodec, ReadOnlyMemory<byte>, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkActorRef actorRef)
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
        ZLinkActorRef actorRef,
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

    private ZLinkBackendActorRef ResolveActorRefForBinding(string actorId, string actorType)
    {
        if (runtime.TryGetCreatedActorState(actorId, actorType, out var state))
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

    private static ZLinkActorRemoteAddress ToRemoteAddress(ZLinkBackendActorRef actorRef)
    {
        return new ZLinkActorRemoteAddress(
            string.Empty,
            actorRef.NodeRid,
            actorRef.Generation);
    }

    private static ActorRef ToPublicActorRef(ZLinkBackendActorRef actorRef)
        => new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);

    private static void EnsureConcreteRemoteAddress(ZLinkActorRemoteAddress remoteAddress)
    {
        if (remoteAddress.TargetNodeRid.IsEmpty || remoteAddress.ActorGeneration == 0)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor remote address requires a target SpotNode routing id and concrete actor generation.",
                isRetriable: false);
        }
    }

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
        ZLinkActorRef actorRef,
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
