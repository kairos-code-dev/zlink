namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotActorRouter
{
    public async ValueTask<bool> TrySubmitAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (!node.TryResolveEntrySpotActorPacket(actor.GetType(), header, out var descriptor)
                || descriptor is null)
            {
                continue;
            }

            await node.InvokeEntrySpotActorPacketAsync(
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
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (!node.TryResolveEntrySpotActorPacket(actor.GetType(), header, out var descriptor)
                || descriptor is null)
            {
                continue;
            }

            var reply = await node.InvokeEntrySpotActorPacketForReplyAsync(
                        descriptor,
                        actor,
                        header,
                        body,
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
        ZLinkSpotActorLifecycleContext context,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            context,
            targetNodeRid,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.TryResolveEntrySpotActorJoined(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask NotifyLeftAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleContext context,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            context,
            targetNodeRid,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.TryResolveEntrySpotActorLeft(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    private static async ValueTask NotifyLifecycleAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleContext context,
        RoutingId? targetNodeRid,
        TryResolveLifecycle resolve,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (targetNodeRid is not null && node.Node.RoutingId != targetNodeRid)
            {
                continue;
            }

            try
            {
                if (resolve(node, actor.GetType(), out var descriptor)
                    && descriptor is not null)
                {
                    await node.InvokeEntrySpotActorLifecycleAsync(
                            descriptor,
                            actor,
                            context,
                            cancellationToken)
                        .ConfigureAwait(false);
                }
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
