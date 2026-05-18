namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        BindActorContext(actor, state);
        await state.ExecuteLockedAsync(
            () =>
            {
                state.SessionId = stream.SessionId;
                state.Stream = stream;
            },
            cancellationToken).ConfigureAwait(false);

        var node = getActorSpotNode();
        if (node is not null
            && state.NativeActorRef is { } actorRef
            && stream is ZLinkManagedStream managedStream)
        {
            await managedStream.BindActorAsync(
                    actorRef,
                    runtime.Registration.DefaultTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var actorId = actor.ActorId;
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        BindActorContext(actor, state);

        var disconnect = await state.ExecuteLockedAsync(
            () =>
            {
                if (!string.Equals(state.SessionId, stream.SessionId, StringComparison.Ordinal))
                {
                    return (ShouldDisconnect: false, Activation: (ZLinkSpotActivation?)null);
                }

                state.SessionId = null;
                state.Stream = null;
                var currentActivation = state.Activation;
                state.Activation = null;

                if (currentActivation is not null && currentActivation.IsDisposed)
                {
                    return (ShouldDisconnect: true, Activation: (ZLinkSpotActivation?)null);
                }

                return (ShouldDisconnect: true, Activation: currentActivation);
            },
            cancellationToken).ConfigureAwait(false);

        if (!disconnect.ShouldDisconnect)
        {
            return;
        }

        var node = getActorSpotNode();
        if (node is not null
            && state.NativeActorRef is { } actorRef
            && stream is ZLinkManagedStream managedStream)
        {
            try
            {
                await managedStream.UnbindActorAsync(
                        actor.ActorId,
                        runtime.Registration.DefaultTimeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (ZlinkException)
            {
            }
        }

        if (disconnect.Activation is not null)
        {
            await disconnect.Activation.DisconnectActorAsync(actor, cancellationToken).ConfigureAwait(false);
        }
        else
        {
            await actor.OnDisconnectedAsync(cancellationToken).ConfigureAwait(false);
        }

        _actorSessions.TryRemove(actorId, state);
    }
}
