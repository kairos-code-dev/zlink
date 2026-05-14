namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);

        var previousActivation = await state.ExecuteLockedAsync(
            () => state.Activation,
            cancellationToken).ConfigureAwait(false);

        if (ReferenceEquals(previousActivation, activation))
        {
            return;
        }

        if (previousActivation is not null && !previousActivation.IsDisposed)
        {
            await previousActivation.LeaveActorAsync(actor, cancellationToken).ConfigureAwait(false);
        }

        await state.ExecuteLockedAsync(
            () =>
            {
                state.Activation = activation;
            },
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        EnsureActorContext(actor, state);
        var currentSpotRid = activation.SpotRid;

        var shouldPrune = await state.ExecuteLockedAsync(
            () =>
            {
                if (ReferenceEquals(state.Activation, activation))
                {
                    state.Activation = null;
                    return state.SessionId is null;
                }

                return false;
            },
            cancellationToken).ConfigureAwait(false);

        var node = getActorSpotNode();
        if (node is not null && state.NativeActorRef is { } actorRef)
        {
            try
            {
                await node.LeaveActorAsync(
                        actorRef,
                        currentSpotRid,
                        runtime.Registration.DefaultTimeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (ZlinkException)
            {
            }
        }

        if (shouldPrune)
        {
            _actorSessions.TryRemove(actor.ActorId, state);
        }
    }
}
