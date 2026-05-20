namespace Zlink.Framework.Runtime.Actors;

internal sealed partial class ZLinkActorSessionManager
{
    private readonly record struct ActorDisconnectPlan(
        bool ShouldDisconnect,
        ZLinkSpotActivation? Activation);

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        BindActorContext(actor, state);
        await BindStreamAsync(state, stream, cancellationToken).ConfigureAwait(false);

        await TryBindNativeActorAsync(state, stream, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var actorId = actor.ActorId;
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        BindActorContext(actor, state);

        var disconnect = await ClearStreamBindingAsync(state, stream, cancellationToken)
            .ConfigureAwait(false);

        if (!disconnect.ShouldDisconnect)
        {
            return;
        }

        await TryUnbindNativeActorAsync(state, actor.ActorId, stream, cancellationToken)
            .ConfigureAwait(false);

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

    private async ValueTask BindStreamAsync(
        ZLinkActorRuntimeState state,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        await state.ExecuteLockedAsync(
            () =>
            {
                state.SessionId = stream.SessionId;
                state.Stream = stream;
            },
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<ActorDisconnectPlan> ClearStreamBindingAsync(
        ZLinkActorRuntimeState state,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        return await state.ExecuteLockedAsync(
            () =>
            {
                if (!string.Equals(state.SessionId, stream.SessionId, StringComparison.Ordinal))
                {
                    return new ActorDisconnectPlan(false, null);
                }

                state.SessionId = null;
                state.Stream = null;
                var currentActivation = state.Activation;
                state.Activation = null;

                if (currentActivation is not null && currentActivation.IsDisposed)
                {
                    return new ActorDisconnectPlan(true, null);
                }

                return new ActorDisconnectPlan(true, currentActivation);
            },
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask TryBindNativeActorAsync(
        ZLinkActorRuntimeState state,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        if (getActorSpotNode() is null
            || state.NativeActorRef is not { } actorRef
            || stream is not ZLinkManagedStream managedStream)
        {
            return;
        }

        await managedStream.BindActorAsync(
                actorRef,
                runtime.Registration.DefaultTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask TryUnbindNativeActorAsync(
        ZLinkActorRuntimeState state,
        string actorId,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        if (getActorSpotNode() is null
            || state.NativeActorRef is null
            || stream is not ZLinkManagedStream managedStream)
        {
            return;
        }

        try
        {
            await managedStream.UnbindActorAsync(
                    actorId,
                    runtime.Registration.DefaultTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZlinkException)
        {
        }
    }
}
