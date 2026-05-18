namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDispatchRouter(
    ZLinkFrameworkRuntime runtime,
    IServiceProvider services,
    ZLinkActorSessionRegistry actorSessions,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext)
{
    private readonly ZLinkActorPacketDispatcher _packetDispatcher = new(services);

    public async ValueTask SubmitByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        await SubmitAsync(actor, header, payload, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        return await SubmitForReplyAsync(actor, state, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask SubmitAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var actorId = actor.ActorId;
        var state = actorSessions.GetOrCreate(actor.ActorId);
        var shouldPrune = false;
        ensureActorContext(actor, state);

        try
        {
            shouldPrune = await state.ExecuteDispatchAsync(
                    header,
                    ct => SubmitByCurrentLocationAsync(actor, state, header, payload, ct),
                    cancellationToken)
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
        Message payload,
        CancellationToken cancellationToken)
    {
        return await state.ExecuteDispatchAsync(
                header,
                ct => SubmitByCurrentLocationForReplyAsync(actor, state, header, payload, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<bool> SubmitByCurrentLocationAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var activation = await state.ExecuteLockedAsync(
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

        if (activation.currentActivation is null)
        {
            if (await runtime.TrySubmitEntrySpotActorAsync(
                    actor,
                    state,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false))
            {
                return activation.prune;
            }

            await _packetDispatcher.DispatchAsync(state, actor, header, payload, cancellationToken)
                .ConfigureAwait(false);
            return activation.prune;
        }

        await activation.currentActivation.SubmitActorAsync(actor, state, header, payload, cancellationToken)
            .ConfigureAwait(false);
        return activation.prune;
    }

    private async ValueTask<byte[]> SubmitByCurrentLocationForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
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
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var entryResult = await runtime.TrySubmitEntrySpotActorForReplyAsync(
                actor,
                state,
                header,
                payload,
                cancellationToken)
            .ConfigureAwait(false);
        if (entryResult.Handled)
        {
            return entryResult.Reply
                ?? throw new InvalidOperationException(
                    $"Entry Spot actor request handler for '{header.Name}' returned no reply.");
        }

        return await DispatchLocalForReplyAsync(actor, state, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchLocalAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        await _packetDispatcher.DispatchAsync(state, actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<byte[]> DispatchLocalForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        return await _packetDispatcher.DispatchForReplyAsync(state, actor, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }
}
