namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings = new(runtime);
    private readonly ZLinkSessionActorRelay _relay = new(runtime);
    private IZLinkActor? _attachedActor;

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        string actorId,
        string actorType,
        CancellationToken cancellationToken)
    {
        EnsureLocalActorExists(actorId, actorType);
        var remoteAddress = runtime.ResolveLocalActorRemoteAddress(actorId, actorType);

        return await BindHandleAsync(
                context,
                actorId,
                actorType,
                remoteAddress,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        string actorId,
        string actorType,
        ZLinkActorRemoteAddress remoteAddress,
        CancellationToken cancellationToken)
    {
        EnsureConcreteRemoteAddress(remoteAddress);
        var isRemote = !IsLocalAddress(remoteAddress.RouterChannelId, remoteAddress.TargetNodeRid);
        if (!isRemote)
        {
            EnsureLocalActorExists(actorId, actorType);
        }

        Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync =
            isRemote
                ? NotifyRemoteActorDisconnectedAsync
                : NotifyLocalActorDisconnectedAsync;
        return await _bindings.BindAsync(
            context,
            context.SessionId,
            actorId,
            actorType,
            remoteAddress.RouterChannelId,
            remoteAddress.TargetNodeRid,
            remoteAddress.ActorGeneration,
            isRemote,
            notifyDisconnectedAsync,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> BindHandleAsync(
        ZLinkSessionContext context,
        IZLinkActorRef actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        if (!actor.IsRemote)
        {
            return await BindHandleAsync(
                    context,
                    actor.ActorId,
                    actor.ActorType,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return await BindHandleAsync(
                context,
                actor.ActorId,
                actor.ActorType,
                actor.RemoteAddress,
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

        var remoteAddress = actorRef.RemoteAddressSnapshot;
        if (!actorRef.IsRemote)
        {
            await DispatchLocalAsync(actorRef, header, payload, replyRawAsync, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await _relay.DispatchRemoteAsync(
                actorRef,
                remoteAddress,
                header,
                payload,
                replyRawAsync,
                cancellationToken)
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

    private bool IsLocalAddress(string routerChannelId, RoutingId targetNodeRid)
    {
        return targetNodeRid.Equals(runtime.ResolveSessionRouterId(routerChannelId));
    }

    private static void EnsureConcreteRemoteAddress(ZLinkActorRemoteAddress remoteAddress)
    {
        if (remoteAddress.ActorGeneration != 0)
        {
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            "Actor remote address requires a concrete actor generation.",
            isRetriable: false);
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

    private async ValueTask NotifyLocalActorDisconnectedAsync(
        ZLinkActorRef actor,
        CancellationToken cancellationToken)
    {
        await runtime.NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask NotifyRemoteActorDisconnectedAsync(
        ZLinkActorRef actor,
        CancellationToken cancellationToken)
    {
        await _relay.NotifyDisconnectedAsync(actor, cancellationToken)
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
}
