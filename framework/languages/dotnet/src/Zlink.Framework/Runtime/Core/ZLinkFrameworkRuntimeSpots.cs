using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string channelName)
    {
        return _spotFacade.GetPublisherBundle(channelName);
    }

    internal async ValueTask<ZLinkSpotCreateResult> CreateSpotAsync(
        string spotName,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.CreateAsync(spotName, createParts, cancellationToken);
    }

    internal async ValueTask<ZLinkSpotCreateResult> GetOrCreateSpotAsync(
        string spotName,
        RoutingId spotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.GetOrCreateAsync(spotName, spotRid, createParts, cancellationToken);
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

    internal async ValueTask<bool> RemoveSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.RemoveAsync(spotRid, cancellationToken);
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
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return;
        }

        await _spots.NotifyEntrySpotActorJoinedAsync(
                _state,
                actor,
                info,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorLeftAsync(
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken = default)
    {
        if (_state is null)
        {
            return;
        }

        await _spots.NotifyEntrySpotActorLeftAsync(
                _state,
                actor,
                info,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotRouterConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.GetRouterConnectionsAsync(spotNodeName, cancellationToken);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotPubSubConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.GetPubSubConnectionsAsync(spotNodeName, cancellationToken);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotChannelClientConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.GetChannelClientConnectionsAsync(spotNodeName, channelName, cancellationToken);
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotPublisherConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        return await _spotFacade.GetPublisherConnectionsAsync(spotNodeName, channelName, cancellationToken);
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
