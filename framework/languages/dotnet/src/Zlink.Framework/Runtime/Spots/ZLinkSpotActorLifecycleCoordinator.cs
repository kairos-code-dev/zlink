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
        actors.Add(actor);
        await runtime.JoinActorToSpotAsync(activation, actor, cancellationToken);
        if (ReferenceEquals(previousActivation, activation)) return;

        if (previousActivation is null)
            await runtime.NotifyEntrySpotActorLeftAsync(actor, activation.NodeRid, cancellationToken)
                .ConfigureAwait(false);

        if (actorHandlers() is { } handlers
            && handlers.TryResolveJoined(actor.GetType(), out var descriptor)
            && descriptor is not null)
            await handlerInvoker().InvokeActorLifecycleAsync(descriptor, actor, cancellationToken)
                .ConfigureAwait(false);
    }

    public async ValueTask LeaveAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        _ = actors;
        _ = actorHandlers;
        _ = handlerInvoker;
        await runtime.JoinActorEntrySpotAsync(activation.NodeRid, actor, ZLinkMessage.Empty, cancellationToken)
            .ConfigureAwait(false);
    }
}