namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings = new(runtime);
    private IZLinkActor? _attachedActor;

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
            actorId,
            actorType,
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
        var actorRef = new ZLinkBackendActorRef(
            remoteAddress.TargetNodeRid,
            actorId,
            remoteAddress.ActorGeneration);
        await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
        return await _bindings.BindAsync(
            context,
            actorId,
            actorType,
            remoteAddress,
            isRemote: true,
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
                    actor.ActorId,
                    actor.ActorType,
                    actor.RemoteAddress,
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
        using (payload)
        {
            if (header.RequestSeq is not null)
            {
                var reply = await runtime.SubmitActorForReplyAsync(
                        actorRef.ActorId,
                        header,
                        payload,
                        cancellationToken)
                    .ConfigureAwait(false);
                await replyRawAsync(header, header.Codec, reply, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await runtime.SubmitActorByIdAsync(
                    actorRef.ActorId,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
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

    private static void EnsureConcreteRemoteAddress(ZLinkActorRemoteAddress remoteAddress)
    {
        if (remoteAddress.TargetNodeRid.IsEmpty || remoteAddress.ActorGeneration == 0)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor remote address requires a target ActorGateway node and concrete actor generation.",
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

    private static ValueTask SendBoundActorAsync(
        ZLinkManagedStream managedStream,
        ZLinkActorRef actorRef,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using (payload)
        {
            using var headerPart = Message.FromBytes(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span);
            using var bodyPart = payload.Move();
            if (!managedStream.SendBoundActor(
                    actorRef.ActorId,
                    new[] { headerPart, bodyPart },
                    SendFlags.None))
            {
                throw new InvalidOperationException("Actor session relay failed.");
            }
        }

        return ValueTask.CompletedTask;
    }
}
