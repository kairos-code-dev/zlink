namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    public async ValueTask<ZLinkSpotActivation?> JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.Context.ActorId);
        BindActorContext(actor, state);

        var previousActivation = await state.ExecuteLockedAsync(
            () => state.Activation,
            cancellationToken).ConfigureAwait(false);

        if (ReferenceEquals(previousActivation, activation)) return previousActivation;

        await state.ExecuteLockedAsync(
            () => state.JoinSpot(activation),
            cancellationToken).ConfigureAwait(false);
        return previousActivation;
    }

    public async ValueTask RestoreActorSpotAfterFailedCommitAsync(
        ZLinkSpotActivation failedTarget,
        ZLinkSpotActivation? previousActivation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.Context.ActorId);
        await state.ExecuteLockedAsync(
            () =>
            {
                if (!ReferenceEquals(state.Activation, failedTarget)) return;
                if (previousActivation is null)
                    state.LeaveSpotIfCurrent(failedTarget);
                else
                    state.JoinSpot(previousActivation);
            },
            cancellationToken).ConfigureAwait(false);
    }
}
