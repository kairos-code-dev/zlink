namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkActorFacade(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    IServiceProvider services,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkComponentState> getState,
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
        string spotId,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var state = getState();
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode();
        var localActivation = spots.GetActivationBySpotId(state, spotId);

        if (localActivation is null
            && node is not null
            && actorState.NativeActorRef is { } actorRef)
            return await _remoteJoiner.JoinAsync(
                state,
                spotId,
                actor,
                actorRef,
                node,
                request,
                cancellationToken).ConfigureAwait(false);

        ZLinkSpotActorJoinResult joinResult;
        var sourceActivation = actorState.Activation;
        if (localActivation is not null
            && sourceActivation is not null
            && !ReferenceEquals(sourceActivation, localActivation))
        {
            joinResult = await localActivation.AdmitActorJoinFromCallerTurnAsync(
                    actor,
                    request,
                    cancellationToken)
                .ConfigureAwait(false);
            if (joinResult.Accepted)
            {
                // The caller owns the source actor turn. Commit source lifecycle
                // there before entering the target commit so the target never
                // posts back into a queue held by this join call.
                await sourceActivation.NotifyActorLeftAfterManagedJoinSpotAsync(
                        actor,
                        cancellationToken)
                    .ConfigureAwait(false);
                await localActivation.CommitActorJoinFromCallerTurnAsync(actor, cancellationToken)
                    .ConfigureAwait(false);
            }
        }
        else if (localActivation is not null)
            joinResult = await localActivation.JoinActorAsync(actor, request, cancellationToken)
                .ConfigureAwait(false);
        else
            joinResult = await spots.JoinActorAsync(
                state,
                spotId,
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
