namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        BindActorContext(actor, state);

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
        BindActorContext(actor, state);
        var currentSpotRid = activation.SpotRid;

        await state.ExecuteLockedAsync(
            () =>
            {
                if (ReferenceEquals(state.Activation, activation))
                {
                    state.Activation = null;
                }
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

    }
}
