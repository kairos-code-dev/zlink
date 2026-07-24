namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    public IZLinkSpotCreateCall Create(string spotType)
    {
        var stableType = RequireSpotType(spotType);
        return new ZLinkSpotCreateCall(
            Registration.DefaultRequestTimeout,
            (mesh, request, profile, affinity, timeout, cancellation) =>
                SubmitUserSpotAsync(
                    stableType,
                    RoutingId.From(Guid.NewGuid().ToString("N")),
                    mesh,
                    request,
                    profile,
                    affinity,
                    timeout,
                    false,
                    cancellation));
    }

    public IZLinkSpotGetOrCreateCall GetOrCreate(
        RoutingId spotRid,
        string spotType)
    {
        var stableType = RequireSpotType(spotType);
        if (spotRid.IsEmpty)
            throw new ArgumentException("Spot RID is required.", nameof(spotRid));
        return new ZLinkSpotGetOrCreateCall(
            Registration.DefaultRequestTimeout,
            (mesh, request, profile, affinity, timeout, cancellation) =>
                SubmitUserSpotAsync(
                    stableType,
                    spotRid,
                    mesh,
                    request,
                    profile,
                    affinity,
                    timeout,
                    true,
                    cancellation));
    }

    private async ValueTask<ZLinkSpotCreateResult> SubmitUserSpotAsync(
        string stableType,
        RoutingId spotRid,
        string? meshName,
        ZLinkMessage request,
        string? placementProfile,
        string? affinityKey,
        TimeSpan timeout,
        bool joinExisting,
        CancellationToken cancellationToken)
    {
        using var operation = EnterOperation();
        _drainAdmission.RequireSpotAdmission();
        return await _spots.CreateByStableTypeAsync(
                GetOrStartState(),
                stableType,
                spotRid,
                meshName,
                request,
                placementProfile,
                affinityKey,
                timeout,
                joinExisting,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string meshName)
    {
        return _spots.GetPublisherBundle(GetOrStartState(), meshName);
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

    public async ValueTask<SpotRef?> FindAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
        return await _spots.ResolveAsync(spotRid, cancellationToken)
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
        SpotRef spot,
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
        return await _spots.CloseAsync(GetOrStartState(), spot, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        using var operation = EnterOperation();
        return await _spots.CloseLegacyAsync(GetOrStartState(), spotRid, cancellationToken)
            .ConfigureAwait(false);
    }

    private static string RequireSpotType(string value)
    {
        if (string.IsNullOrWhiteSpace(value)
            || System.Text.Encoding.UTF8.GetByteCount(value) > byte.MaxValue
            || value.Contains('\0'))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.InvalidConfiguration,
                "Spot type must be 1..255 UTF-8 bytes without NUL.");
        return value;
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

    /// <summary>The registered MeshNode for a physical mesh. ChannelName
    /// select-one calls (IZLinkRouteClient) submit through this node's entry
    /// spot so weight, ready and drain admission stay Core-owned (spec 11 §3).</summary>
    internal ZLinkSpotNodeRuntime GetMeshNodeRuntime(string meshName)
    {
        var state = GetOrStartState();
        lock (state.SyncRoot)
        {
            return state.SpotNodes.TryGetValue(meshName, out var nodeRuntime)
                   ? nodeRuntime
                   : throw new ZLinkConfigurationException(
                       $"RouteMesh '{meshName}' is not registered.");
        }
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
