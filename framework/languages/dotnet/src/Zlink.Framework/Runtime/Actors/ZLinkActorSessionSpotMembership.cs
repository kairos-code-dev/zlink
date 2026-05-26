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
            await previousActivation.NotifyActorLeftAfterManagedJoinSpotAsync(
                    actor,
                    new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JoinSpot),
                    cancellationToken)
                .ConfigureAwait(false);
        }

        await state.ExecuteLockedAsync(
            () =>
            {
                state.Activation = activation;
            },
            cancellationToken).ConfigureAwait(false);
    }

}
