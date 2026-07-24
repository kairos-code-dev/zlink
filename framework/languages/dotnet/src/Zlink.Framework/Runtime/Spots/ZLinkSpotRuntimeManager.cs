namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRuntimeManager(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkLocationLifecycle? locationLifecycle)
{
    private readonly ZLinkFrameworkRegistration _frameworkRegistration = registration;
    private readonly IZLinkLocationStore? _locationStore =
        registration.Locations.ResolveStore();
    private readonly ZLinkEntrySpotActorRouter _entrySpotActors = new(runtime);

    private readonly ZLinkSpotNodeInitializer _nodeInitializer = new(
        services,
        runtime,
        backendAdapterFactory,
        registration,
        locationLifecycle);

    public ZLinkEntrySpotActorRouter EntrySpotActors => _entrySpotActors;

    public async ValueTask InitializeSpotNodesAsync(ZLinkFrameworkComponentState state)
    {
        await _nodeInitializer.InitializeAsync(state).ConfigureAwait(false);
    }

    public ZLinkSpotPublisherBundle GetPublisherBundle(
        ZLinkFrameworkComponentState state,
        string channelName)
    {
        if (state.SpotNodes.TryGetValue(channelName, out var node))
            return node.GetOrCreatePublisherBundle(channelName);

        throw new ZLinkConfigurationException(
            $"SPOT publisher mesh '{channelName}' is not registered.");
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        ZLinkFrameworkComponentState state,
        Type spotType,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotType);
        var relocation = node.Registration.SpotRelocations
            .SingleOrDefault(pair => pair.Value.InstanceType == spotType);
        if (!string.IsNullOrEmpty(relocation.Key)
            && _locationStore is not null)
        {
            return await TryRemoteCreateAsync(
                    node,
                    relocation.Key,
                    RoutingId.From(Guid.NewGuid().ToString("N")),
                    request,
                    null,
                    null,
                    _frameworkRegistration.DefaultRequestTimeout,
                    false,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        return await node.CreateAsync(spotType, request, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        ZLinkFrameworkComponentState state,
        Type spotType,
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotType);
        var relocation = node.Registration.SpotRelocations
            .SingleOrDefault(pair => pair.Value.InstanceType == spotType);
        if (!string.IsNullOrEmpty(relocation.Key)
            && _locationStore is not null)
        {
            return await TryRemoteCreateAsync(
                    node,
                    relocation.Key,
                    requestedSpotRid: spotRid,
                    request,
                    null,
                    null,
                    _frameworkRegistration.DefaultRequestTimeout,
                    true,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        return await node.GetOrCreateAsync(
            spotType,
            spotRid,
            request,
            cancellationToken);
    }

    internal async ValueTask<ZLinkSpotCreateResult> CreateByStableTypeAsync(
        ZLinkFrameworkComponentState state,
        string stableType,
        RoutingId requestedSpotRid,
        string? meshName,
        ZLinkMessage request,
        string? placementProfile,
        string? affinityKey,
        TimeSpan timeout,
        bool joinExisting,
        CancellationToken cancellationToken)
    {
        var candidates = state.SpotNodes.Values
            .Where(node => node.Registration.ObjectRoleSelected
                && (meshName is null || string.Equals(
                    node.Registration.SpotNodeName,
                    meshName,
                    StringComparison.Ordinal)))
            .ToArray();
        if (candidates.Length == 0)
            throw new ZLinkFrameworkException(
                meshName is null
                    ? ZLinkFrameworkErrorKind.ObjectClientNotConfigured
                    : ZLinkFrameworkErrorKind.MeshNotFound,
                meshName is null
                    ? "No object client MeshNode is registered."
                    : $"Object MeshNode '{meshName}' is not registered.");
        if (meshName is null && candidates.Length > 1)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.MeshSelectionRequired,
                "More than one object client MeshNode is registered.");
        return await TryRemoteCreateAsync(
                candidates[0],
                stableType,
                requestedSpotRid,
                request,
                placementProfile,
                affinityKey,
                timeout,
                joinExisting,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkSpotCreateResult> TryRemoteCreateAsync(
        ZLinkSpotNodeRuntime source,
        string stableType,
        RoutingId requestedSpotRid,
        ZLinkMessage request,
        string? placementProfile,
        string? affinityKey,
        TimeSpan timeout,
        bool joinExisting,
        CancellationToken cancellationToken)
    {
        var meshName = source.Registration.SpotNodeName;
        var descriptors = await _locationStore!.ListMeshNodesAsync(
                meshName, cancellationToken)
            .ConfigureAwait(false);
        var target = descriptors
            .Where(candidate =>
                candidate.Rid != source.Node.RoutingId
                && candidate.State == ZLinkFrameworkRuntimeState.Serving
                && candidate.ObjectRole == ZLinkMeshNodeObjectRole.Server
                && candidate.PlacementWeight > 0
                && candidate.ObjectCapabilities.Any(capability =>
                    capability.ObjectKind == ZLinkPlacementObjectKind.UserSpot
                    && string.Equals(
                        capability.StableType,
                        stableType,
                        StringComparison.Ordinal)
                    && (placementProfile is null
                        || capability.PlacementProfiles.Contains(
                            placementProfile))))
            .OrderByDescending(static candidate => candidate.PlacementWeight)
            .ThenBy(static candidate => candidate.Rid.ToHex(), StringComparer.Ordinal)
            .FirstOrDefault();
        if (target is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
                $"No Ready User Spot target is available for '{stableType}'.",
                true);

        var encoded = request.Encode(_frameworkRegistration.Codecs);
        var applicationPayload = ZLinkApplicationPayloadEnvelopeCodec.Encode(
            string.Empty,
            encoded.ContentType,
            encoded.Payload.Bytes.Span);
        var creationIntentReference =
            ZLinkInlineCreationIntentCodec.Encode(applicationPayload);
        var key = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(requestedSpotRid);
        var owner = new ZLinkLocationOwnerToken(
            target.OwnerId, target.LeaseGeneration);
        var creating = ZLinkUserSpotAuthorityPayloadCodec.Encode(
            new ZLinkUserSpotAuthorityPayload(
                ZLinkUserSpotAuthorityState.Creating,
                stableType,
                requestedSpotRid,
                owner.OwnerId,
                checked((ulong)owner.LeaseGeneration),
                meshName,
                target.Rid,
                target.LifecycleGeneration));
        var reserved = await _locationStore.ReserveAsync(
                new ZLinkObjectReservationRequest(
                    ZLinkPlacementObjectKind.UserSpot,
                    key,
                    stableType,
                    placementProfile,
                    affinityKey,
                    creationIntentReference,
                    System.Security.Cryptography.SHA256.HashData(
                        applicationPayload),
                    applicationPayload.Length,
                    new ZLinkMeshNodeDescriptorKey(meshName, target.Rid),
                    target.LifecycleGeneration,
                    owner,
                    creating,
                    1),
                cancellationToken)
            .ConfigureAwait(false);
        if (reserved is not ZLinkObjectReserveResult.Reserved reservation)
        {
            if (reserved is ZLinkObjectReserveResult.TypeMismatch)
                throw SpotTypeMismatch(requestedSpotRid, stableType);
            if (reserved is ZLinkObjectReserveResult.AlreadyExists existing)
            {
                if (!joinExisting)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RoutingIdConflict,
                        $"User Spot '{requestedSpotRid}' already exists.");
                return await JoinExistingAsync(
                        requestedSpotRid,
                        stableType,
                        existing.Current,
                        timeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            throw reserved switch
            {
                ZLinkObjectReserveResult.PlacementCapacityExhausted =>
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
                        $"User Spot capacity is exhausted for '{stableType}'.",
                        true),
                _ => new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"User Spot '{requestedSpotRid}' reservation changed.",
                    true)
            };
        }
        var snapshot = reservation.Reservation;
        var deadline = checked((ulong)DateTimeOffset.UtcNow
            .Add(timeout)
            .ToUnixTimeMilliseconds());
        (UserSpotCreateCompletion Completion, IReadOnlyList<Message> Reply) result;
        try
        {
            result = await source.Node.CreateUserSpotAsync(
                    target.Rid,
                    requestedSpotRid,
                    stableType,
                    new UserSpotReservationFence(
                        snapshot.ReservationVersion,
                        snapshot.StoreVersion,
                        snapshot.ObjectGeneration,
                        snapshot.AuthorityOwnerGeneration,
                        target.Rid,
                        target.LifecycleGeneration,
                        owner.OwnerId,
                        checked((ulong)owner.LeaseGeneration),
                        1),
                    deadline,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            await _locationStore.AbortAsync(snapshot, CancellationToken.None)
                .ConfigureAwait(false);
            throw;
        }
        ZLinkMessage? reply = null;
        try
        {
            if (result.Reply.Count > 1)
            {
                var header = ZLinkEnvelopeCodec.DecodeHeader(result.Reply);
                reply = ZLinkMessage.FromEnvelopePayload(
                    header.ContentType,
                    result.Reply[1],
                    _frameworkRegistration.Codecs);
            }
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(result.Reply);
        }
        return new ZLinkSpotCreateResult(
            new SpotRef(
                result.Completion.SpotRid,
                result.Completion.ObjectGeneration,
                meshName,
                target.Rid),
            result.Completion.Result switch
            {
                UserSpotCreateResult.Existing => ZLinkSpotCreateState.Existing,
                UserSpotCreateResult.Created => ZLinkSpotCreateState.Created,
                _ => ZLinkSpotCreateState.Rejected
            },
            reply);
    }

    private async ValueTask<ZLinkSpotCreateResult> JoinExistingAsync(
        RoutingId spotRid,
        string stableType,
        ZLinkAuthoritySnapshot current,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow.Add(timeout);
        while (true)
        {
            if (current.Allocation.ObjectKind != ZLinkPlacementObjectKind.UserSpot
                || !string.Equals(
                    current.Allocation.StableType,
                    stableType,
                    StringComparison.Ordinal))
                throw SpotTypeMismatch(spotRid, stableType);
            if (!ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                    current.Payload.Span, out var authority))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"User Spot '{spotRid}' authority is invalid.",
                    true);
            if (current.Allocation.State == ZLinkPlacementAllocationState.Active
                && authority.State == ZLinkUserSpotAuthorityState.Ready)
                return new ZLinkSpotCreateResult(
                    new SpotRef(
                        spotRid,
                        current.ObjectGeneration,
                        authority.MeshName,
                        authority.NodeRid),
                    ZLinkSpotCreateState.Existing,
                    null);
            if (current.Allocation.State != ZLinkPlacementAllocationState.Pending
                || authority.State != ZLinkUserSpotAuthorityState.Creating)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"User Spot '{spotRid}' creation changed.",
                    true);
            if (DateTimeOffset.UtcNow >= deadline)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.DeadlineExceeded,
                    $"Timed out while joining User Spot '{spotRid}' creation.",
                    true);

            await Task.Delay(
                    TimeSpan.FromMilliseconds(10),
                    cancellationToken)
                .ConfigureAwait(false);
            var read = await _locationStore!.ReadAuthorityAsync(
                    ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotRid),
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"User Spot '{spotRid}' creation was aborted.",
                    true);
            current = found.Snapshot;
        }
    }

    private static ZLinkFrameworkException SpotTypeMismatch(
        RoutingId spotRid,
        string stableType) =>
        new(
            ZLinkFrameworkErrorKind.SpotTypeMismatch,
            $"User Spot '{spotRid}' is not registered as '{stableType}'.");

    public async ValueTask<ZLinkSpotInfo?> GetAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            var info = await node.GetAsync(spotRid, cancellationToken);
            if (info is not null) return info;
        }

        return null;
    }

    public async ValueTask<SpotRef?> ResolveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        if (_locationStore is null) return null;
        var read = await _locationStore.ReadAuthorityAsync(
                ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotRid),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || found.Snapshot.Allocation.State
                != ZLinkPlacementAllocationState.Active
            || found.Snapshot.Allocation.ObjectKind
                != ZLinkPlacementObjectKind.UserSpot
            || !ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span, out var authority)
            || authority.State != ZLinkUserSpotAuthorityState.Ready)
            return null;
        return new SpotRef(
            spotRid,
            found.Snapshot.ObjectGeneration,
            authority.MeshName,
            authority.NodeRid);
    }

    public async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        ZLinkFrameworkComponentState state,
        CancellationToken cancellationToken)
    {
        var results = new List<ZLinkSpotInfo>();
        foreach (var node in state.SpotNodes.Values) results.AddRange(await node.ListAsync(cancellationToken));

        return results
            .OrderBy(static info => info.SpotRid.ToHex(), StringComparer.Ordinal)
            .ToArray();
    }

    public async ValueTask<bool> CloseAsync(
        ZLinkFrameworkComponentState state,
        SpotRef spot,
        CancellationToken cancellationToken)
    {
        var spotRid = spot.SpotRid;
        if (_locationStore is not null)
        {
            var key = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotRid);
            var read = await _locationStore.ReadAuthorityAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (read is ZLinkAuthorityReadResult.Found found)
            {
                if (!ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                        found.Snapshot.Payload.Span, out var authority))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotMoving,
                        $"User Spot '{spotRid}' authority is invalid.",
                        true);
                if (found.Snapshot.Allocation.State
                        != ZLinkPlacementAllocationState.Active
                    || found.Snapshot.Allocation.ObjectKind
                        != ZLinkPlacementObjectKind.UserSpot
                    || authority.State != ZLinkUserSpotAuthorityState.Ready)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotMoving,
                        $"User Spot '{spotRid}' is not Ready.",
                        true);
                if (found.Snapshot.ObjectGeneration != spot.ObjectGeneration)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotGenerationStale,
                        $"User Spot '{spotRid}' generation is stale.");
                if (!string.Equals(
                        authority.MeshName, spot.MeshName,
                        StringComparison.Ordinal)
                    || authority.NodeRid != spot.NodeRid)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotMoving,
                        $"User Spot '{spotRid}' owner changed.",
                        true);
                var source = state.SpotNodes.Values.FirstOrDefault(node =>
                    string.Equals(
                        node.Registration.SpotNodeName,
                        authority.MeshName,
                        StringComparison.Ordinal));
                if (source is not null && authority.NodeRid != source.Node.RoutingId)
                {
                    var deadline = checked((ulong)DateTimeOffset.UtcNow
                        .Add(_frameworkRegistration.DefaultRequestTimeout)
                        .ToUnixTimeMilliseconds());
                    var closed = await source.Node.CloseUserSpotAsync(
                            authority.NodeRid,
                            new UserSpotCloseFence(
                                spotRid,
                                found.Snapshot.ObjectGeneration,
                                authority.NodeRid,
                                authority.NodeGeneration,
                                found.Snapshot.AuthorityOwnerGeneration,
                                found.Snapshot.StoreVersion),
                            deadline,
                            _frameworkRegistration.DefaultRequestTimeout,
                            cancellationToken)
                        .ConfigureAwait(false);
                    return closed.Closed;
                }
            }
        }
        foreach (var node in state.SpotNodes.Values)
            if (await node.CloseAsync(spotRid, cancellationToken))
                return true;

        return false;
    }

    internal ValueTask<bool> CloseLegacyAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        CancellationToken cancellationToken) =>
        CloseLocalAsync(state, spotRid, cancellationToken);

    private static async ValueTask<bool> CloseLocalAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
            if (await node.CloseAsync(spotRid, cancellationToken))
                return true;
        return false;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> JoinActorAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var activation = GetActivationBySpotRid(state, spotRid)
                         ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await activation.JoinActorAsync(actor, request, cancellationToken);
    }

    public async ValueTask<bool> TryNotifyJoinedSpotActorDisconnectedAsync(
        ZLinkFrameworkComponentState state,
        string actorId,
        CancellationToken cancellationToken)
    {
        foreach (var activation in state.SpotNodes.Values.SelectMany(static node => node.Spots))
        {
            if (!activation.TryGetJoinedActor(actorId, out var actor) || actor is null) continue;

            await activation.NotifyActorDisconnectedAsync(actor, cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        return false;
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot(
        ZLinkFrameworkComponentState state,
        string spotNodeName)
    {
        return GetNode(state, spotNodeName).GetMonitoringSnapshot();
    }

    private static ZLinkSpotNodeRuntime GetNode(
        ZLinkFrameworkComponentState state,
        string spotNodeName)
    {
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new ZLinkConfigurationException($"SPOT node '{spotNodeName}' is not registered.");
    }

    private static ZLinkSpotNodeRuntime GetNodeForSpotFactory(
        ZLinkFrameworkComponentState state,
        Type spotType)
    {
        foreach (var node in state.SpotNodes.Values)
            if (node.SpotFactories.Contains(spotType))
                return node;

        throw new ZLinkConfigurationException($"SPOT factory '{spotType}' is not registered.");
    }

    public ZLinkSpotActivation? GetActivationBySpotRid(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            var activation = node.Spots.FirstOrDefault(current => current.SpotRid == spotRid);
            if (activation is not null) return activation;
        }

        return null;
    }
}

internal readonly record struct EntrySpotActorReplyDispatchResult(
    bool Handled,
    ZLinkActorReply? Reply);
