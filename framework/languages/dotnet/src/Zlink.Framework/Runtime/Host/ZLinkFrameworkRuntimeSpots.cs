namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string channelName)
    {
        return _spots.GetPublisherBundle(GetOrStartState(), channelName);
    }

    internal ValueTask<ZLinkSpotCreateResult> CreateSpotAsync<TSpot>(
        ZLinkMessage request,
        CancellationToken cancellationToken)
        where TSpot : IZLinkSpot
    {
        return _spots.CreateAsync(GetOrStartState(), typeof(TSpot), request, cancellationToken);
    }

    internal ValueTask<ZLinkSpotCreateResult> GetOrCreateSpotAsync<TSpot>(
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken)
        where TSpot : IZLinkSpot
    {
        return _spots.GetOrCreateAsync(GetOrStartState(), typeof(TSpot), spotRid, request, cancellationToken);
    }

    internal ValueTask<ZLinkSpotInfo?> GetSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return _spots.GetAsync(GetOrStartState(), spotRid, cancellationToken);
    }

    internal async ValueTask<ZLinkSpotActivation?> GetSpotActivationByRidAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        return _spots.GetActivationBySpotRid(state, spotRid);
    }

    internal ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListSpotsAsync(
        CancellationToken cancellationToken)
    {
        return _spots.ListAsync(GetOrStartState(), cancellationToken);
    }

    internal ValueTask<bool> CloseSpotAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return _spots.CloseAsync(GetOrStartState(), spotRid, cancellationToken);
    }

    internal IZLinkBackendSpotNode? GetActorSpotNode()
    {
        return _state?.SpotNodes.Values
            .SingleOrDefault(static node => node.Registration.ActorFactories.Count > 0)
            ?.Node;
    }

    internal ValueTask<bool> TrySubmitEntrySpotActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrStartState();
        return _spots.EntrySpotActors.TryAsync(
                state,
                actor,
                header,
                payload,
                cancellationToken);
    }

    internal ValueTask<EntrySpotActorReplyDispatchResult> TrySubmitEntrySpotActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        bool callerOwnsDispatchTurn,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrStartState();
        return _spots.EntrySpotActors.TrySubmitForReplyAsync(
                state,
                actor,
                runtimeState,
                header,
                payload,
                callerOwnsDispatchTurn,
                cancellationToken);
    }

    internal async ValueTask NotifyEntrySpotActorJoinedAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        if (_state is null) return;

        await _spots.EntrySpotActors.NotifyJoinedAsync(
                _state,
                actor,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorCreatedAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        await NotifyEntrySpotActorCreatedAsync(
                actor,
                ZLinkMessage.Empty,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorCreatedAsync(
        IZLinkActor actor,
        ZLinkMessage createRequest,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        if (_state is null) return;

        await _spots.EntrySpotActors.NotifyCreatedAsync(
                _state,
                actor,
                createRequest,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyEntrySpotActorLeftAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        if (_state is null) return;

        await _spots.EntrySpotActors.NotifyLeftAsync(
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
        if (_state is null) return false;

        return await _spots.EntrySpotActors.TryNotifyDisconnectedAsync(
                _state,
                actor,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<bool> TryNotifyJoinedSpotActorDisconnectedAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        if (_state is null) return false;

        if (_actorSessionManager.TryGetCreatedActorState(actorId, out var actorState)
            && actorState is { Actor: { } actor, LiveActivation: { } activation })
        {
            await activation.NotifyActorDisconnectedAsync(actor, cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        return await _spots.TryNotifyJoinedSpotActorDisconnectedAsync(
                _state,
                actorId,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZLinkSpotMonitoringSnapshot GetSpotMonitoringSnapshot(string spotNodeName)
    {
        return _spots.GetMonitoringSnapshot(GetOrStartState(), spotNodeName);
    }

    internal ZLinkSpotNodeRuntime GetSpotNodeRuntime(string spotNodeName)
    {
        var state = GetOrStartState();
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new ZLinkConfigurationException($"SPOT node '{spotNodeName}' is not registered.");
    }
}
