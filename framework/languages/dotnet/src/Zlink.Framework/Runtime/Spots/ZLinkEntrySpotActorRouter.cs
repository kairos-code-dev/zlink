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

            var previousDispatch = runtimeState.CurrentDispatch;
            runtimeState.CurrentDispatch = new ZLinkActorDispatchState(header);
            try
            {
                await node.InvokeEntrySpotActorPacketAsync(
                        descriptor,
                        actor,
                        header,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                runtimeState.CurrentDispatch = previousDispatch;
            }

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

            var previousDispatch = runtimeState.CurrentDispatch;
            runtimeState.CurrentDispatch = new ZLinkActorDispatchState(header);
            try
            {
                var reply = await node.InvokeEntrySpotActorPacketForReplyAsync(
                        descriptor,
                        actor,
                        header,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false);
                return new EntrySpotActorReplyDispatchResult(true, reply);
            }
            finally
            {
                runtimeState.CurrentDispatch = previousDispatch;
            }
        }

        return new EntrySpotActorReplyDispatchResult(false, null);
    }

    public async ValueTask NotifyJoinedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            info,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.TryResolveEntrySpotActorJoined(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask NotifyLeftAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        await NotifyLifecycleAsync(
            state,
            actor,
            info,
            static (ZLinkSpotNodeRuntime node, Type actorType, out ZLinkSpotActorLifecycleDescriptor? descriptor) =>
                node.TryResolveEntrySpotActorLeft(actorType, out descriptor),
            cancellationToken).ConfigureAwait(false);
    }

    private static async ValueTask NotifyLifecycleAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        TryResolveLifecycle resolve,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (!resolve(node, actor.GetType(), out var descriptor)
                || descriptor is null)
            {
                continue;
            }

            try
            {
                await node.InvokeEntrySpotActorLifecycleAsync(
                        descriptor,
                        actor,
                        info,
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
