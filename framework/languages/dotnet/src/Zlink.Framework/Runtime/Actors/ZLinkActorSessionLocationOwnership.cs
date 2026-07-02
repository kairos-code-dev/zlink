namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    /// <summary>
    /// Ownership-loss rule (location resolver store draft, section 9):
    /// another owner replaced this actor's location row, so the local
    /// instance must deactivate to keep single-activation. The context is
    /// invalidated first so in-flight callers fail fast, then local state
    /// is cleared and the native actor is destroyed best-effort.
    /// </summary>
    public async ValueTask DeactivateActorOnOwnershipLossAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        if (!_actorSessions.TryGet(actorId, out var state)) return;

        var actorRef = await state.ExecuteLockedAsync<ZLinkBackendActorRef?>(
            () =>
            {
                var nativeRef = state.NativeActorRef;
                state.InvalidateContext();
                return nativeRef;
            },
            cancellationToken).ConfigureAwait(false);

        var boundSession = await state.ExecuteLockedAsync(
            () => state.ClearAfterDestroy(),
            CancellationToken.None).ConfigureAwait(false);
        if (boundSession is { } session) runtime.RemoveActorSessionBinding(actorId, session.BindingToken);

        _actorSessions.RemoveIfCurrent(actorId, state);

        if (actorRef is not { } nativeActor || getActorSpotNode() is not { } node) return;

        try
        {
            await node.DestroyActorAsync(
                    nativeActor,
                    runtime.Registration.DefaultRequestTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"ownership-loss native actor destroy failed for '{actorId}': {exception.Message}");
        }
    }

    /// <summary>
    /// The actor moved to another node whose runtime claimed the location
    /// itself (hosting handoff): this owner releases its row and stops
    /// tracking. A release racing the new owner's Takeover is ignored as
    /// stale by the store, which is the intended fencing outcome.
    /// </summary>
    public async ValueTask ReleaseActorLocationAfterMoveAsync(
        ZLinkActorRuntimeState state,
        CancellationToken cancellationToken = default)
    {
        if (LocationLifecycle is not { } lifecycle) return;

        await lifecycle.ReleaseActorAsync(
                state.ActorType ?? string.Empty,
                state.ActorId,
                cancellationToken)
            .ConfigureAwait(false);
    }

    /// <summary>
    /// The actor moved to another node's entry spot through the native
    /// join path, where no framework runtime claims the row on the target:
    /// this owner keeps the row and renews it with the new node rid so
    /// resolvers keep finding the actor.
    /// </summary>
    public async ValueTask RenewActorLocationAfterEntrySpotMoveAsync(
        ZLinkActorRuntimeState state,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken = default)
    {
        if (LocationLifecycle is not { } lifecycle) return;

        await lifecycle.NotifyActorMovedToEntrySpotAsync(
                state.ActorType ?? string.Empty,
                state.ActorId,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }
}
