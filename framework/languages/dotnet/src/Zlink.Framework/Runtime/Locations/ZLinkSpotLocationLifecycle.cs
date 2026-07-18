namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkSpotLocationLifecycle(
    ZLinkLocationRuntime runtime,
    ZLinkStoreLocationResolvers resolver)
{
    private readonly object _gate = new();
    private readonly Dictionary<string, TrackedSpot> _spots = new(StringComparer.Ordinal);

    internal async ValueTask<ZLinkLocationWriteStatus> ClaimAsync(
        string meshName,
        RoutingId spotRid,
        string? spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken = default)
    {
        // The store issues the spot generation on claim; the row carries no
        // endpoint because peers reach a spot through its owner MeshNode
        // descriptor (40-location-runtime §2.2).
        var row = new ZLinkSpotLocation(
            meshName,
            spotRid,
            SpotGeneration: 0,
            OwnerNodeRid: nodeRid,
            OwnerNodeGeneration: 0,
            spotKind,
            SpotType: spotType ?? string.Empty,
            OwnerId: string.Empty,
            UpdatedAt: default);
        var result = await runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            var existing = await resolver.ResolveSpotRowAsync(
                new ZLinkSpotLocationKey(meshName, spotRid),
                cancellationToken).ConfigureAwait(false);
            if (existing?.OwnerNodeRid == nodeRid)
            {
                result = await runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        if (result.Status == ZLinkLocationWriteStatus.Stored)
        {
            // The claim row is serialized before the store issues the spot
            // generation, so one renew persists the issued value into the
            // row (the store keeps row JSON exactly as written).
            _ = await runtime.WriteSpotAsync(
                    row with { SpotGeneration = result.Generation },
                    ZLinkLocationWriteIntent.Renew,
                    cancellationToken)
                .ConfigureAwait(false);
            var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(meshName, spotRid));
            lock (_gate)
            {
                _spots[canonical] = new TrackedSpot(spotRid, result.Generation, deactivate);
            }
        }

        return result.Status;
    }

    /// <summary>Store-issued generation of a spot this runtime claimed;
    /// actor membership rows carry it as their spot lifecycle fence.</summary>
    internal bool TryGetTrackedGeneration(RoutingId spotRid, out ulong generation)
    {
        lock (_gate)
        {
            foreach (var tracked in _spots.Values)
            {
                if (!tracked.SpotRid.Equals(spotRid)) continue;
                generation = tracked.Generation;
                return true;
            }
        }

        generation = 0;
        return false;
    }

    internal async ValueTask ReleaseAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkSpotLocationKey(meshName, spotRid);
        var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(key);
        TrackedSpot? tracked;
        lock (_gate)
        {
            if (!_spots.TryGetValue(canonical, out tracked))
            {
                return;
            }
        }

        var result = await runtime.RemoveSpotAsync(key, tracked.Generation, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status is not (ZLinkLocationWriteStatus.Stored or ZLinkLocationWriteStatus.IgnoredStale))
            throw new InvalidOperationException(
                $"SPOT '{spotRid}' location release was rejected with '{result.Status}'.");

        lock (_gate)
        {
            if (_spots.TryGetValue(canonical, out var current) && ReferenceEquals(current, tracked))
                _spots.Remove(canonical);
        }
    }

    internal Func<CancellationToken, ValueTask>? TakeOwnershipLostDeactivation(string canonicalKey)
    {
        lock (_gate)
        {
            return _spots.Remove(canonicalKey, out var spot) ? spot.Deactivate : null;
        }
    }

    internal void ResetGeneration()
    {
        lock (_gate) _spots.Clear();
    }

    private sealed record TrackedSpot(
        RoutingId SpotRid,
        ulong Generation,
        Func<CancellationToken, ValueTask>? Deactivate);
}
