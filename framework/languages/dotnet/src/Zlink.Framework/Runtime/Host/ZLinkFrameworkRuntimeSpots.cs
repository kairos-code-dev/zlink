namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string channelName)
    {
        return _spots.GetPublisherBundle(GetOrStartState(), channelName);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return CreateAsync<TSpot>(ZLinkMessage.Empty, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        using var operation = EnterOperation();
        _drainAdmission.RequireSpotAdmission();
        return await _spots.CreateAsync(GetOrStartState(), typeof(TSpot), request, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        using var operation = EnterOperation();
        _drainAdmission.RequireSpotAdmission();
        return await _spots.GetOrCreateAsync(
                GetOrStartState(), typeof(TSpot), spotRid, request, cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return GetOrCreateAsync<TSpot>(spotRid, ZLinkMessage.Empty, cancellationToken);
    }

    public async ValueTask<ZLinkSpotInfo?> FindAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
        return await _spots.GetAsync(GetOrStartState(), spotRid, cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkSpotActivation?> GetSpotActivationByRidAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        using var operation = EnterOperation();
        var state = await GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        return _spots.GetActivationBySpotRid(state, spotRid);
    }

    public async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
        return await _spots.ListAsync(GetOrStartState(), cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
        return await _spots.CloseAsync(GetOrStartState(), spotRid, cancellationToken)
            .ConfigureAwait(false);
    }

    internal IZLinkBackendSpotNode? GetActorSpotNode()
    {
        return _state?.SpotNodes.Values
            .SingleOrDefault(static node =>
                node.Registration.Router is not null
                && node.Registration.ActorFactories.Count > 0)
            ?.Node;
    }

    internal IZLinkBackendSpotNode GetActorClientSpotNode()
    {
        return GetActorClientSpotNodeRuntime().Node;
    }

    /// <summary>Any router-capable node, or null before startup — the
    /// bound-session relay planes live on the router plane even on hosts
    /// without local actor factories (a session host binding remote actors).</summary>
    internal IZLinkBackendSpotNode? GetRouterSpotNodeOrNull()
    {
        return _state?.SpotNodes.Values
            .FirstOrDefault(static node => node.Registration.Router is not null)
            ?.Node;
    }

    internal ZLinkSpotNodeRuntime GetActorClientSpotNodeRuntime()
    {
        var state = GetOrStartState();
        lock (state.SyncRoot)
        {
            return state.SpotNodes.Values
                       .FirstOrDefault(static node => node.Registration.Router is not null)
                   ?? throw new ZLinkConfigurationException(
                       "Actor client requires a router-capable SPOT node.");
        }
    }

    internal ValueTask<bool> TrySubmitEntrySpotActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return ExecuteOperationAsync(() =>
        {
            var state = GetOrStartState();
            return _spots.EntrySpotActors.TryAsync(
                state, actor, header, payload, cancellationToken);
        });
    }

    internal ValueTask<EntrySpotActorReplyDispatchResult> TrySubmitEntrySpotActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        bool callerOwnsDispatchTurn,
        CancellationToken cancellationToken = default)
    {
        return ExecuteOperationAsync(() =>
        {
            var state = GetOrStartState();
            return _spots.EntrySpotActors.TrySubmitForReplyAsync(
                state, actor, runtimeState, header, payload, callerOwnsDispatchTurn, cancellationToken);
        });
    }

    internal async ValueTask NotifyEntrySpotActorJoinedAsync(
        IZLinkActor actor,
        RoutingId? targetNodeRid = null,
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
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
        using var operation = EnterOperation();
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
        using var operation = EnterOperation();
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
        using var operation = EnterOperation();
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
        using var operation = EnterOperation();
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
        return ExecuteOperation(() => _spots.GetMonitoringSnapshot(GetOrStartState(), spotNodeName));
    }

    internal ZLinkSpotNodeRuntime GetSpotNodeRuntime(string spotNodeName)
    {
        return ExecuteOperation(() =>
        {
            var state = GetOrStartState();
            return state.SpotNodes.TryGetValue(spotNodeName, out var node)
                ? node
                : throw new ZLinkConfigurationException($"SPOT node '{spotNodeName}' is not registered.");
        });
    }
}
