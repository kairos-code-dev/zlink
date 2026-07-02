namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    public async ValueTask DestroyActorAsync(
        RoutingId entrySpotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);

        if (!_actorSessions.TryGet(actor.ActorId, out var state)) return;

        var actorRef = await state.ExecuteLockedAsync<ZLinkBackendActorRef?>(
            () =>
            {
                if (state.Actor is null) return null;

                if (!ReferenceEquals(state.Actor, actor)) return null;

                if (state.Activation is not null)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actor.ActorId}' must leave its current SPOT before destroy.");

                var nativeActor = state.NativeActorRef
                                  ?? throw new ZLinkFrameworkException(
                                      ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                      $"Actor '{actor.ActorId}' does not have a native Actor ref.");

                if (nativeActor.NodeRid != entrySpotNodeRid)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actor.ActorId}' is not owned by this Entry Spot.");

                if (!state.TryBeginDestroy()) return null;

                return nativeActor;
            },
            cancellationToken).ConfigureAwait(false);

        if (actorRef is not { } currentActorRef) return;

        var actorType = state.ActorType;
        try
        {
            var node = getActorSpotNode()
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           "Actor destroy requires a router-capable SpotNode.",
                           false);

            await node.DestroyActorAsync(
                    currentActorRef,
                    runtime.Registration.DefaultRequestTimeout,
                    cancellationToken)
                .ConfigureAwait(false);

            var boundSession = await state.ExecuteLockedAsync(
                () => state.ClearAfterDestroy(),
                CancellationToken.None).ConfigureAwait(false);

            if (boundSession is { } session) runtime.RemoveActorSessionBinding(actor.ActorId, session.BindingToken);

            _actorSessions.RemoveIfCurrent(actor.ActorId, state);

            if (LocationLifecycle is { } lifecycle)
                await lifecycle.ReleaseActorAsync(
                        actorType ?? string.Empty,
                        actor.ActorId,
                        CancellationToken.None)
                    .ConfigureAwait(false);
        }
        catch
        {
            await state.ExecuteLockedAsync(
                state.ResetDestroying,
                CancellationToken.None).ConfigureAwait(false);
            throw;
        }
    }
}