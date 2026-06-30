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
        await BindStreamAsync(state, stream, cancellationToken).ConfigureAwait(false);

        await TryBindNativeActorAsync(state, stream, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorId);
        BindActorContext(actor, state);

        var shouldUnbind = await ClearStreamBindingAsync(state, stream, cancellationToken)
            .ConfigureAwait(false);

        if (!shouldUnbind) return;

        await TryUnbindNativeActorAsync(state, actor.ActorId, stream, cancellationToken)
            .ConfigureAwait(false);
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

    private async ValueTask<bool> ClearStreamBindingAsync(
        ZLinkActorRuntimeState state,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        return await state.ExecuteLockedAsync(
            () =>
            {
                if (!string.Equals(state.SessionId, stream.SessionId, StringComparison.Ordinal)) return false;

                state.SessionId = null;
                state.Stream = null;
                return true;
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
            return;

        await managedStream.BindActorAsync(
                actorRef,
                runtime.Registration.DefaultRequestTimeout,
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
            return;

        try
        {
            await managedStream.UnbindActorAsync(
                    actorId,
                    runtime.Registration.DefaultRequestTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZlinkException)
        {
        }
    }
}