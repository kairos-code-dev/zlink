namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkActorFacade(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    IServiceProvider services,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorEntrySpotJoinCoordinator _entrySpotJoin = new(
        registration,
        spots,
        actorSessionManager,
        getState,
        getActorSpotNode,
        runtime.Flow);

    private readonly ZLinkActorRemoteJoiner _remoteJoiner = new(
        runtime,
        registration,
        services,
        spots,
        actorSessionManager);

    public async ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var state = getState();
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode();
        var localActivation = spots.GetActivationBySpotRid(state, spotRid);

        if (localActivation is null
            && node is not null
            && actorState.NativeActorRef is { } actorRef)
            return await _remoteJoiner.JoinAsync(
                state,
                spotRid,
                actor,
                actorRef,
                node,
                request,
                cancellationToken).ConfigureAwait(false);

        ZLinkSpotActorJoinResult joinResult;
        if (localActivation is not null)
            joinResult = await localActivation.JoinActorAsync(actor, request, cancellationToken)
                .ConfigureAwait(false);
        else
            joinResult = await spots.JoinActorAsync(
                state,
                spotRid,
                actor,
                request,
                cancellationToken).ConfigureAwait(false);
        var reply = joinResult.Reply ?? ZLinkMessage.Empty;
        return joinResult.Accepted
            ? new ZLinkActorJoinResult.Accepted(ToActorRef(actorState), reply)
            : new ZLinkActorJoinResult.Rejected(reply);
    }

    public async ValueTask<ZLinkActorJoinResult> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        return await _entrySpotJoin.JoinAsync(spotNodeRid, actor, request, cancellationToken)
            .ConfigureAwait(false);
    }

    private static ActorRef ToActorRef(ZLinkActorRuntimeState actorState)
    {
        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref.");
        return actorRef.ToNative();
    }
}
