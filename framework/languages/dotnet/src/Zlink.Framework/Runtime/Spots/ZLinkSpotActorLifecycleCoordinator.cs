namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorLifecycleCoordinator(
    ZLinkFrameworkRuntime runtime,
    ZLinkSpotActivation activation,
    ZLinkSpotActorMembership actors,
    Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker)
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

        var context = ToPublicLifecycleContext(
            actor,
            previousActivation,
            activation,
            ZLinkSpotActorLifecycleReason.JoinSpot);
        if (previousActivation is null)
        {
            var entryLeftContext = ToPublicLifecycleContext(
                actor,
                previousActivation: null,
                activation,
                ZLinkSpotActorLifecycleReason.JoinSpot);
            await runtime.NotifyEntrySpotActorLeftAsync(actor, entryLeftContext, cancellationToken)
                .ConfigureAwait(false);
        }

        if (actorHandlers() is { } handlers
            && handlers.TryResolveJoined(actor.GetType(), out var descriptor)
            && descriptor is not null)
        {
            await handlerInvoker().InvokeActorLifecycleAsync(descriptor, actor, context, cancellationToken)
                .ConfigureAwait(false);
        }
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

        var context = ToPublicLifecycleContext(
            actor,
            activation,
            currentActivation: null,
            ZLinkSpotActorLifecycleReason.LeaveSpot);
        if (actorHandlers() is { } handlers
            && handlers.TryResolveLeft(actor.GetType(), out var descriptor)
            && descriptor is not null)
        {
            await handlerInvoker().InvokeActorLifecycleAsync(descriptor, actor, context, cancellationToken)
                .ConfigureAwait(false);
        }

        var entryJoinedContext = ToPublicLifecycleContext(
            actor,
            activation,
            currentActivation: null,
            ZLinkSpotActorLifecycleReason.LeaveSpot);
        await runtime.NotifyEntrySpotActorJoinedAsync(actor, entryJoinedContext, cancellationToken)
            .ConfigureAwait(false);
    }

    private static ZLinkSpotActorLifecycleContext ToPublicLifecycleContext(
        IZLinkActor actor,
        ZLinkSpotActivation? previousActivation,
        ZLinkSpotActivation? currentActivation,
        ZLinkSpotActorLifecycleReason reason)
    {
        _ = actor;
        return new ZLinkSpotActorLifecycleContext(
            previousActivation?.SpotRid,
            currentActivation?.SpotRid,
            JoinEpoch: 0,
            reason,
            NativeFlags: 0)
        {
            ActorId = actor.ActorId
        };
    }
}
