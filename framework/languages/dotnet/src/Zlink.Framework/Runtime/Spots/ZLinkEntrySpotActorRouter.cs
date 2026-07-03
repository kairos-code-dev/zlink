namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotActorRouter
{
    public async ValueTask<bool> TryAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        _ = runtimeState;
        foreach (var node in state.SpotNodes.Values)
        {
            var dispatch = node.EntrySpotActorDispatch;
            if (!dispatch.TryResolvePacket(actor.GetType(), header, out var descriptor)
                || descriptor is null)
                continue;

            // The only caller (the dispatch router's send path) already
            // holds this actor's dispatch turn; re-entering the mailbox
            // here deadlocks the actor permanently.
            await dispatch.InvokePacketAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);

            return true;
        }

        return false;
    }

    public async ValueTask<EntrySpotActorReplyDispatchResult> TrySubmitForReplyAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        bool callerOwnsDispatchTurn,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            var dispatch = node.EntrySpotActorDispatch;
            if (!dispatch.TryResolvePacket(actor.GetType(), header, out var descriptor)
                || descriptor is null)
                continue;

            // A caller inside the actor's dispatch turn (the dispatch
            // router) must not re-enter the mailbox — that deadlocks the
            // actor. Turnless callers (the entry pump) still serialize
            // through it.
            var reply = callerOwnsDispatchTurn
                ? await dispatch.InvokePacketForReplyAsync(
                        descriptor,
                        actor,
                        header,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false)
                : await runtimeState.ExecuteDispatchAsync(
                        header,
                        ct => dispatch.InvokePacketForReplyAsync(
                            descriptor,
                            actor,
                            header,
                            body,
                            ct),
                        cancellationToken)
                    .ConfigureAwait(false);
            return new EntrySpotActorReplyDispatchResult(true, reply);
        }

        return new EntrySpotActorReplyDispatchResult(false, null);
    }

    public async ValueTask SubmitResolvedAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        await runtimeState.ExecuteDispatchAsync(
                header,
                operation,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyJoinedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            targetNodeRid,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.EntrySpotActorDispatch.TryResolveJoined(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask NotifyCreatedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkMessage createRequest,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            createRequest,
            targetNodeRid,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.EntrySpotActorDispatch.TryResolveCreated(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask NotifyLeftAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            targetNodeRid,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.EntrySpotActorDispatch.TryResolveLeft(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<bool> TryNotifyDisconnectedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        var handled = false;
        foreach (var node in state.SpotNodes.Values)
        {
            if (targetNodeRid is not null && node.Node.RoutingId != targetNodeRid) continue;

            var dispatch = node.EntrySpotActorDispatch;
            if (!dispatch.TryResolveDisconnected(actor.GetType(), out var descriptor)
                || descriptor is null)
                continue;

            await dispatch.InvokeDisconnectedAsync(
                    descriptor,
                    actor,
                    cancellationToken)
                .ConfigureAwait(false);
            handled = true;
        }

        return handled;
    }


    private static async ValueTask NotifyLifecycleAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        TryResolveLifecycle resolve,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
                state,
                actor,
                null,
                targetNodeRid,
                resolve,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask NotifyLifecycleAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkMessage? request,
        RoutingId? targetNodeRid,
        TryResolveLifecycle resolve,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (targetNodeRid is not null && node.Node.RoutingId != targetNodeRid) continue;

            try
            {
                if (resolve(node, actor.GetType(), out var descriptor)
                    && descriptor is not null)
                    await node.EntrySpotActorDispatch.InvokeLifecycleAsync(
                            descriptor,
                            actor,
                            request,
                            cancellationToken)
                        .ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch
            {
            }
        }
    }

    private delegate bool TryResolveLifecycle(
        ZLinkSpotNodeRuntime node,
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor);
}