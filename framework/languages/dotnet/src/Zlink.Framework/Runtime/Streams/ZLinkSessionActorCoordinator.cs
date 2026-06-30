namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings = new(runtime);
    private readonly ZLinkBoundActorRelaySender _relaySender = new(runtime.Registration.DefaultRequestTimeout);

    public IReadOnlyCollection<IZLinkSessionActor> BoundActors => _bindings.BoundActors;

    public async ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        var actorRef = ResolveActorRefForBinding(actor);
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

    public IZLinkSessionActor? FindActor(string actorId)
    {
        return _bindings.FindActor(actorId);
    }

    public async ValueTask RelayToActorAsync(
        IZLinkSessionActor actor,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZLinkActorReply, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkSessionActor actorRef)
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");

        if (stream is ZLinkManagedStream managedStream)
        {
            await _relaySender.SendAsync(managedStream, actorRef, header, payload, cancellationToken)
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
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");

        await runtime.NotifyActorDisconnectedAsync(actorRef.Ref, cancellationToken)
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
        await CleanupBindingsAsync(context, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchLocalAsync(
        ZLinkSessionActor actorRef,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZLinkActorReply, CancellationToken, ValueTask> replyRawAsync,
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
            await replyRawAsync(header, reply, cancellationToken)
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

    private ZLinkBackendActorRef ResolveActorRefForBinding(IZLinkActor actor)
    {
        var actorId = actor.ActorId;
        if (runtime.TryGetCreatedActorState(actorId, out var state))
        {
            if (state.Actor is not null && !ReferenceEquals(state.Actor, actor))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' is already created with a different actor instance.");

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
    {
        return new ActorRef(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);
    }

    private static void EnsureConcreteActorRef(ActorRef actor)
    {
        if (actor.NodeRid.IsEmpty || actor.Generation == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor ref requires a target SpotNode routing id and concrete actor generation.",
                false);
    }

    private async ValueTask BindNativeActorAsync(
        ZLinkBackendActorRef actorRef,
        CancellationToken cancellationToken)
    {
        if (stream is not ZLinkManagedStream managedStream) return;

        await managedStream.BindActorAsync(
                actorRef,
                runtime.Registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }
}