namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorLifecycleCoordinator(
    ZLinkFrameworkRuntime runtime,
    ZLinkSpotActivation activation,
    ZLinkSpotActorMembership actors,
    Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker,
    Func<IZLinkSpot> spot)
{
    public async ValueTask JoinAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        var previousActivation =
            runtime.GetOrCreateActorState(actor.ActorId).Activation;
        actors.Add(activation.SpotName, actor);
        await runtime.JoinActorToSpotAsync(activation, actor, cancellationToken);
        if (ReferenceEquals(previousActivation, activation))
        {
            return;
        }

        var info = ToPublicLifecycleInfo(
            actor,
            previousActivation,
            activation,
            ZLinkSpotActorLifecycleKind.Joined);
        if (previousActivation is null)
        {
            var entryLeftInfo = ToPublicLifecycleInfo(
                actor,
                previousActivation: null,
                activation,
                ZLinkSpotActorLifecycleKind.Left);
            await runtime.NotifyEntrySpotActorLeftAsync(actor, entryLeftInfo, cancellationToken)
                .ConfigureAwait(false);
        }

        if (actorHandlers() is { } handlers
            && handlers.TryResolveJoined(actor.GetType(), out var descriptor)
            && descriptor is not null)
        {
            await handlerInvoker().InvokeActorLifecycleAsync(descriptor, actor, info, cancellationToken)
                .ConfigureAwait(false);
        }

        await InvokeLifecycleCallbackAsync(
                static (spot, info, ct) => spot.OnActorJoinedAsync(info, ct),
                info,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask LeaveAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        var wasCurrent = ReferenceEquals(
            runtime.GetOrCreateActorState(actor.ActorId).Activation, activation);
        actors.RemoveIfCurrent(actor);
        await runtime.LeaveActorFromSpotAsync(activation, actor, cancellationToken);
        if (!wasCurrent)
        {
            return;
        }

        var info = ToPublicLifecycleInfo(
            actor,
            activation,
            currentActivation: null,
            ZLinkSpotActorLifecycleKind.Left);
        if (actorHandlers() is { } handlers
            && handlers.TryResolveLeft(actor.GetType(), out var descriptor)
            && descriptor is not null)
        {
            await handlerInvoker().InvokeActorLifecycleAsync(descriptor, actor, info, cancellationToken)
                .ConfigureAwait(false);
        }

        await InvokeLifecycleCallbackAsync(
                static (spot, info, ct) => spot.OnActorLeftAsync(info, ct),
                info,
                cancellationToken)
            .ConfigureAwait(false);

        var entryJoinedInfo = ToPublicLifecycleInfo(
            actor,
            activation,
            currentActivation: null,
            ZLinkSpotActorLifecycleKind.Joined);
        await runtime.NotifyEntrySpotActorJoinedAsync(actor, entryJoinedInfo, cancellationToken)
            .ConfigureAwait(false);
    }

    private static ZLinkSpotActorLifecycleInfo ToPublicLifecycleInfo(
        IZLinkActor actor,
        ZLinkSpotActivation? previousActivation,
        ZLinkSpotActivation? currentActivation,
        ZLinkSpotActorLifecycleKind kind)
    {
        return new ZLinkSpotActorLifecycleInfo(
            kind,
            actor.ActorId,
            previousActivation?.NodeRid,
            previousActivation?.SpotRid,
            currentActivation?.NodeRid,
            currentActivation?.SpotRid,
            previousActivation?.SpotName,
            currentActivation?.SpotName,
            PreviousIsEntrySpot: previousActivation is null,
            CurrentIsEntrySpot: currentActivation is null,
            CommitEpoch: 0);
    }

    private async ValueTask InvokeLifecycleCallbackAsync(
        Func<IZLinkSpot, ZLinkSpotActorLifecycleInfo, CancellationToken, ValueTask> callback,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        try
        {
            await callback(spot(), info, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch
        {
            // Lifecycle hooks are notifications after membership has changed.
            // A failing hook must not roll back or stall actor join/leave.
        }
    }
}
