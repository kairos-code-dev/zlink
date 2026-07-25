namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    /// <summary>Starts the runtime when needed and exposes the started
    /// state so the location auto-connect host can wire its per-mesh loops
    /// to the created channel and spot node runtimes.</summary>
    internal ValueTask<ZLinkFrameworkComponentState> EnsureStartedStateAsync(
        CancellationToken cancellationToken) =>
        GetStartedStateAsync(cancellationToken);

    internal async ValueTask<ZLinkResolvedSpotHandle?> ResolveSpotHandleAsync(
        string spotId,
        CancellationToken cancellationToken)
    {
        ZLinkSpotId.RequireCallerProvided(spotId, nameof(spotId));
        if (Registration.Locations.ResolveStore() is not IZLinkAuthorityStore store)
            return null;
        var key = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotId);
        var read = await store.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || !TryResolveSpotAuthority(spotId, found.Snapshot, out var snapshot))
            return null;
        return new ZLinkResolvedSpotHandle(
            snapshot,
            found.Snapshot.AuthorityOwnerGeneration,
            async token =>
            {
                var refreshed = await store.ReadAuthorityAsync(key, token)
                    .ConfigureAwait(false);
                if (refreshed is not ZLinkAuthorityReadResult.Found current
                    || !TryResolveSpotAuthority(
                        spotId,
                        current.Snapshot,
                        out var handle))
                    return null;
                return (handle, current.Snapshot.AuthorityOwnerGeneration);
            });
    }

    internal async ValueTask<ZLinkResolvedSpotHandle?> ResolveInstanceSpotHandleAsync(
        InstanceSpotIntentAddress address,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(address);
        ZLinkSpotId.RequireCallerProvided(
            address.SpotId,
            nameof(address.SpotId));
        var registeredStore = Registration.Locations.ResolveStore();
        if (registeredStore is IZLinkAuthorityStore authorityStore)
        {
            var authorityKey = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(address.SpotId);
            var read = await authorityStore.ReadAuthorityAsync(authorityKey, cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found authorityFound
                || !TryResolveInstanceAuthority(
                    address,
                    authorityFound.Snapshot,
                    out var snapshot))
                return null;
            return new ZLinkResolvedSpotHandle(
                snapshot,
                authorityFound.Snapshot.AuthorityOwnerGeneration,
                async token =>
                {
                    var refreshed = await authorityStore.ReadAuthorityAsync(authorityKey, token)
                        .ConfigureAwait(false);
                    if (refreshed is not ZLinkAuthorityReadResult.Found current
                        || !TryResolveInstanceAuthority(
                            address,
                            current.Snapshot,
                            out var currentHandle))
                        return null;
                    return (
                        currentHandle,
                        current.Snapshot.AuthorityOwnerGeneration);
                });
        }

        var store = registeredStore as IZLinkInstanceSpotLocationStore;
        if (store is null) return null;

        var key = new ZLinkSpotLocationKey(address.SpotId);
        var resolved = await store.ResolveInstanceSpotAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (resolved is not InstanceSpotResolveResult.Found found
            || found.Snapshot.Location.ActivationState != ZLinkSpotActivationState.Ready
            || !string.Equals(
                found.Snapshot.Location.InstanceSpotType,
                address.InstanceSpotType,
                StringComparison.Ordinal))
            return null;

        var row = found.Snapshot.Location;
        return new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot(
                row.MeshName,
                row.OwnerNodeRid,
                row.SpotId,
                row.SpotGeneration,
                ZLinkSpotKind.Instance,
                row.LocationGeneration),
            row.LocationGeneration,
            async token =>
            {
                var refreshed = await store.ResolveInstanceSpotAsync(key, token)
                    .ConfigureAwait(false);
                if (refreshed is not InstanceSpotResolveResult.Found current
                    || current.Snapshot.Location.ActivationState
                    != ZLinkSpotActivationState.Ready)
                    return null;
                var location = current.Snapshot.Location;
                return (
                    new ZLinkSpotHandleSnapshot(
                        location.MeshName,
                        location.OwnerNodeRid,
                        location.SpotId,
                        location.SpotGeneration,
                        ZLinkSpotKind.Instance,
                        location.LocationGeneration),
                    location.LocationGeneration);
            });
    }

    internal async ValueTask<IReadOnlyList<Message>> ActivateInstanceSpotAsync(
        InstanceSpotIntentAddress address,
        IReadOnlyList<Message> parts,
        bool request,
        TimeSpan timeout,
        ReadOnlyMemory<byte> metadata,
        CancellationToken cancellationToken)
    {
        var source = ResolveActorCreationSource(address.MeshName);
        var store = Registration.Locations.ResolveStore()
                    ?? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.InvalidConfiguration,
                        "Instance Spot activation requires a Location Store.");
        var descriptors = await store.ListMeshNodesAsync(
                address.MeshName,
                cancellationToken)
            .ConfigureAwait(false);
        var eligible = descriptors
            .Where(candidate => IsEligibleInstanceCandidate(
                candidate,
                address.InstanceSpotType))
            .OrderBy(static candidate => candidate.Rid.ToHex(), StringComparer.Ordinal)
            .ToArray();
        var selected = ZLinkWeightedSelector.Select(
            eligible,
            static candidate => candidate.PlacementWeight,
            ref _nextInstanceActivationSelection)
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
                $"No Ready Instance Spot target is available for '{address.InstanceSpotType}'.",
                true);
        var deadlineAt = DateTimeOffset.UtcNow.Add(timeout);
        var deadlineUnixMs = checked((ulong)deadlineAt.ToUnixTimeMilliseconds());
        var target = new InstanceSpotActivationTarget(
            address.MeshName,
            selected.Rid,
            selected.LifecycleGeneration,
            address.SpotId,
            address.InstanceSpotType,
            selected.DescriptorRevision.ToString(
                System.Globalization.CultureInfo.InvariantCulture));
        var sourceStatus = source.Node.MeshStatus();
        var sourceSpotId =
            ZLinkSpotAmbientContext.CurrentOrDefault?.SpotId ?? string.Empty;
        var state = _state
                    ?? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.InvalidConfiguration,
                        "Framework runtime is not started.");
        if (state.TryGetSpotNodeByRoutingId(selected.Rid, out var localTarget))
        {
            var local = await localTarget.ActivateInstanceSpotLocalAsync(
                    target,
                    source.Node.RoutingId,
                    sourceStatus.LifecycleGeneration,
                    sourceSpotId,
                    parts.Select(static part =>
                            (ReadOnlyMemory<byte>)part.ToArray())
                        .ToArray(),
                    request,
                    deadlineUnixMs,
                    metadata.IsEmpty ? null : metadata,
                    cancellationToken)
                .ConfigureAwait(false);
            if (local.Result != RequestResult.Ok)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotCreateFailed,
                    "Local Instance Spot activation failed.");
            return local.ReplyParts.Select(Message.From).ToArray();
        }
        return await source.Node.ActivateInstanceSpotAsync(
                target,
                sourceSpotId,
                parts,
                request,
                deadlineUnixMs,
                timeout,
                metadata,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static bool TryResolveInstanceAuthority(
        InstanceSpotIntentAddress address,
        ZLinkAuthoritySnapshot snapshot,
        out ZLinkSpotHandleSnapshot handle)
    {
        handle = default;
        if (snapshot.Allocation.State != ZLinkPlacementAllocationState.Active
            || snapshot.Allocation.ObjectKind != ZLinkPlacementObjectKind.InstanceSpot
            || !ZLinkInstanceSpotAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var payload)
            || payload.State != ZLinkInstanceSpotAuthorityState.Ready
            || !string.Equals(payload.SpotId, address.SpotId, StringComparison.Ordinal)
            || (!string.IsNullOrEmpty(address.InstanceSpotType)
                && !string.Equals(
                payload.StableType,
                address.InstanceSpotType,
                StringComparison.Ordinal)))
            return false;
        handle = new ZLinkSpotHandleSnapshot(
            payload.MeshName,
            payload.NodeRid,
            payload.SpotId,
            snapshot.ObjectGeneration,
            ZLinkSpotKind.Instance,
            snapshot.AuthorityOwnerGeneration);
        return true;
    }

    private static bool TryResolveSpotAuthority(
        string spotId,
        ZLinkAuthoritySnapshot snapshot,
        out ZLinkSpotHandleSnapshot handle)
    {
        handle = default;
        if (snapshot.Allocation.State != ZLinkPlacementAllocationState.Active)
            return false;
        if (snapshot.Allocation.ObjectKind == ZLinkPlacementObjectKind.InstanceSpot
            && ZLinkInstanceSpotAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var instance)
            && instance.State == ZLinkInstanceSpotAuthorityState.Ready
            && string.Equals(instance.SpotId, spotId, StringComparison.Ordinal))
        {
            handle = new ZLinkSpotHandleSnapshot(
                instance.MeshName,
                instance.NodeRid,
                instance.SpotId,
                snapshot.ObjectGeneration,
                ZLinkSpotKind.Instance,
                snapshot.AuthorityOwnerGeneration);
            return true;
        }
        if (snapshot.Allocation.ObjectKind == ZLinkPlacementObjectKind.UserSpot
            && ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var user)
            && user.State == ZLinkUserSpotAuthorityState.Ready
            && string.Equals(user.SpotId, spotId, StringComparison.Ordinal))
        {
            handle = new ZLinkSpotHandleSnapshot(
                user.MeshName,
                user.NodeRid,
                user.SpotId,
                snapshot.ObjectGeneration,
                ZLinkSpotKind.User,
                snapshot.AuthorityOwnerGeneration);
            return true;
        }
        return false;
    }

    private static bool IsEligibleInstanceCandidate(
        ZLinkMeshNodeDescriptor candidate,
        string stableType) =>
        candidate.State == ZLinkFrameworkRuntimeState.Serving
        && candidate.ObjectRole == ZLinkMeshNodeObjectRole.Server
        && candidate.PlacementWeight > 0
        && (candidate.Capacity.Spots.Limit == 0
            || candidate.Capacity.Spots.Active
            + (long)candidate.Capacity.Spots.Reserved
            < candidate.Capacity.Spots.Limit)
        && candidate.ObjectCapabilities.Any(capability =>
            capability.ObjectKind == ZLinkPlacementObjectKind.InstanceSpot
            && string.Equals(
                capability.StableType,
                stableType,
                StringComparison.Ordinal))
        && candidate.Capacity.SpotTypes.Any(capacity =>
            capacity.ObjectKind == ZLinkPlacementObjectKind.InstanceSpot
            && string.Equals(
                capacity.StableType,
                stableType,
                StringComparison.Ordinal)
            && (capacity.Limit == 0
                || capacity.Active + (long)capacity.Reserved
                < capacity.Limit));
}
