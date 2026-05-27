using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDispatchRouter(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorSessionRegistry actorSessions,
    Func<IZLinkActor, ZLinkActorRuntimeState, ZLinkActorContext> ensureActorContext)
{
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

    public async ValueTask<ZLinkActorReply> SubmitForReplyAsync(
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

    public async ValueTask NotifyDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var state = actorSessions.GetOrCreate(actorId);
        var actor = state.Actor
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not active.");

        await state.ExecuteLifecycleAsync(
                ct => NotifyDisconnectedByCurrentLocationAsync(actor, state, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorReply> SubmitForReplyAsync(
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
        var placement = await state.ExecuteLockedAsync(
            () => state.SelectPlacementLocked(pruneWhenSessionless: true),
            cancellationToken).ConfigureAwait(false);

        if (placement.Activation is null)
        {
            await runtime.TrySubmitEntrySpotActorAsync(
                    actor,
                    state,
                    header,
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
            return placement.Prune;
        }

        await placement.Activation.SubmitActorAsync(actor, state, header, payload, cancellationToken)
            .ConfigureAwait(false);
        return placement.Prune;
    }

    private async ValueTask<ZLinkActorReply> SubmitByCurrentLocationForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var placement = await state.ExecuteLockedAsync(
            () => state.SelectPlacementLocked(pruneWhenSessionless: false),
            cancellationToken).ConfigureAwait(false);

        if (placement.Activation is not null)
        {
            return await placement.Activation.SubmitActorForReplyAsync(
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

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
            $"No Spot actor request handler is registered for '{header.Name}'.");
    }

    private async ValueTask NotifyDisconnectedByCurrentLocationAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        CancellationToken cancellationToken)
    {
        var placement = await state.ExecuteLockedAsync(
            () => state.SelectPlacementLocked(pruneWhenSessionless: false),
            cancellationToken).ConfigureAwait(false);

        if (placement.Activation is not null)
        {
            await placement.Activation.NotifyActorDisconnectedAsync(actor, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await runtime.TryNotifyEntrySpotActorDisconnectedAsync(
                actor,
                targetNodeRid: null,
                cancellationToken)
            .ConfigureAwait(false);
    }

}
