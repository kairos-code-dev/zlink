using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string channelName)
    {
        return _spotFacade.GetPublisherBundle(channelName);
    }

    internal async ValueTask<ZLinkSpotCreateResult> CreateSpotAsync<TSpot>(
        Message request,
        CancellationToken cancellationToken)
        where TSpot : IZLinkSpot
    {
        return await _spotFacade.CreateAsync(typeof(TSpot), request, cancellationToken);
    }

    internal async ValueTask<ZLinkSpotCreateResult> GetOrCreateSpotAsync<TSpot>(
        RoutingId spotRid,
        Message request,
        CancellationToken cancellationToken)
        where TSpot : IZLinkSpot
    {
        return await _spotFacade.GetOrCreateAsync(typeof(TSpot), spotRid, request, cancellationToken);
    }

    internal async ValueTask<ZLinkSpotInfo?> GetSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.GetAsync(spotRid, cancellationToken);
    }

    internal async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListSpotsAsync(
        CancellationToken cancellationToken)
    {
        return await _spotFacade.ListAsync(cancellationToken);
    }

    internal async ValueTask<bool> CloseSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.CloseAsync(spotRid, cancellationToken);
    }

    internal IZLinkBackendSpotNode? GetActorSpotNode()
    {
        return _state?.SpotNodes.Values.FirstOrDefault()?.Node;
    }

    internal async ValueTask<bool> TrySubmitEntrySpotActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrStartState();
        return await _spots.TrySubmitEntrySpotActorAsync(
                state,
                actor,
                runtimeState,
                header,
                payload,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<EntrySpotActorReplyDispatchResult> TrySubmitEntrySpotActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrStartState();
        return await _spots.TrySubmitEntrySpotActorForReplyAsync(
                state,
                actor,
                runtimeState,
                header,
                payload,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask SubmitResolvedEntrySpotActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken = default)
    {
        GetOrStartState();
        await _spots.SubmitResolvedEntrySpotActorAsync(
                actor,
                runtimeState,
                header,
                operation,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorJoinedAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return;
        }

        await _spots.NotifyEntrySpotActorJoinedAsync(
                _state,
                actor,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorLeftAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return;
        }

        await _spots.NotifyEntrySpotActorLeftAsync(
                _state,
                actor,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<bool> TryNotifyEntrySpotActorDisconnectedAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return false;
        }

        return await _spots.TryNotifyEntrySpotActorDisconnectedAsync(
                _state,
                actor,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZLinkSpotMonitoringSnapshot GetSpotMonitoringSnapshot(string spotNodeName)
    {
        return _spotFacade.GetMonitoringSnapshot(spotNodeName);
    }

    internal ZLinkSpotNodeRuntime GetSpotNodeRuntime(string spotNodeName)
    {
        var state = GetOrStartState();
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new ZLinkConfigurationException($"SPOT node '{spotNodeName}' is not registered.");
    }
}
