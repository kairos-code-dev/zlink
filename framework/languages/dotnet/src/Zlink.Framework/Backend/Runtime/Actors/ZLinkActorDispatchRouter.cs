namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDispatchRouter(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    ZLinkActorSessionRegistry actorSessions,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext)
{
    public async ValueTask SubmitByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        await SubmitAsync(actor, header, body, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        return await SubmitForReplyAsync(actor, state, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask SubmitAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var actorId = actor.ActorId;
        var state = actorSessions.GetOrCreate(actor.ActorId);
        ZLinkSpotActivation? activation;
        var shouldPrune = false;
        ensureActorContext(actor, state);

        (activation, shouldPrune) = await state.ExecuteLockedAsync(
            () =>
            {
                var currentActivation = state.Activation;
                var prune = false;

                if (currentActivation is not null && currentActivation.IsDisposed)
                {
                    state.Activation = null;
                    currentActivation = null;
                    prune = state.SessionId is null;
                }

                return (currentActivation, prune);
            },
            cancellationToken).ConfigureAwait(false);

        try
        {
            if (activation is null)
            {
                if (await runtime.TrySubmitEntrySpotActorAsync(
                        actor,
                        state,
                        header,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false))
                {
                    return;
                }

                await DispatchLocalAsync(actor, state, header, body, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await activation.SubmitActorAsync(actor, state, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            if (shouldPrune)
            {
                actorSessions.TryRemove(actorId, state);
            }
        }
    }

    private async ValueTask<byte[]> SubmitForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var activation = await state.ExecuteLockedAsync(
            () =>
            {
                var currentActivation = state.Activation;
                if (currentActivation is not null && currentActivation.IsDisposed)
                {
                    state.Activation = null;
                    currentActivation = null;
                }

                return currentActivation;
            },
            cancellationToken).ConfigureAwait(false);

        if (activation is not null)
        {
            return await activation.SubmitActorForReplyAsync(
                    actor,
                    state,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var entryResult = await runtime.TrySubmitEntrySpotActorForReplyAsync(
                actor,
                state,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
        if (entryResult.Handled)
        {
            return entryResult.Reply
                ?? throw new InvalidOperationException(
                    $"Entry Spot actor request handler for '{header.Name}' returned no reply.");
        }

        return await DispatchLocalForReplyAsync(actor, state, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchLocalAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previousDispatch = state.CurrentDispatch;
        state.CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            await state.DispatchAsync(services, actor, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            state.CurrentDispatch = previousDispatch;
        }
    }

    private async ValueTask<byte[]> DispatchLocalForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previousDispatch = state.CurrentDispatch;
        state.CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            return await state.DispatchForReplyAsync(services, actor, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            state.CurrentDispatch = previousDispatch;
        }
    }
}
