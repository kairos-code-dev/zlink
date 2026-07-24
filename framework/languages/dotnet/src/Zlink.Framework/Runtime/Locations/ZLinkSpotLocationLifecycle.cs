namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkSpotLocationLifecycle(
    ZLinkLocationRuntime runtime,
    ZLinkStoreLocationResolvers resolver)
{
    private readonly object _gate = new();
    private readonly Dictionary<string, TrackedSpot> _spots = new(StringComparer.Ordinal);

    internal async ValueTask<ZLinkLocationWriteStatus> ClaimAsync(
        string meshName,
        string spotId,
        ulong spotGeneration,
        string? spotType,
        RoutingId nodeRid,
        ulong nodeGeneration,
        ZLinkSpotKind spotKind,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken = default)
    {
        // The row's SpotGeneration is the spot's core lifecycle generation
        // (the value peers pass back on spot-addressed submits); the store's
        // own generation is a separate owner-token fence. The row carries no
        // endpoint because peers reach a spot through its owner MeshNode
        // descriptor (40-location-runtime §2.2).
        var row = new ZLinkSpotLocation(
            meshName,
            spotId,
            SpotGeneration: spotGeneration,
            OwnerNodeRid: nodeRid,
            OwnerNodeGeneration: nodeGeneration,
            spotKind,
            SpotType: spotType ?? string.Empty,
            OwnerId: string.Empty,
            UpdatedAt: default);
        var result = await runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            var existing = await resolver.ResolveSpotRowAsync(
                new ZLinkSpotLocationKey(spotId),
                cancellationToken).ConfigureAwait(false);
            if (existing?.OwnerNodeRid == nodeRid)
            {
                result = await runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        if (result.Status == ZLinkLocationWriteStatus.Stored)
        {
            var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(spotId));
            lock (_gate)
            {
                _spots[canonical] = new TrackedSpot(
                    spotId, spotGeneration, result.Generation, deactivate);
            }
        }

        return result.Status;
    }

    /// <summary>Core lifecycle generation of a spot this runtime claimed;
    /// actor membership rows carry it as their spot lifecycle fence.</summary>
    internal bool TryGetTrackedGeneration(string spotId, out ulong generation)
    {
        lock (_gate)
        {
            foreach (var tracked in _spots.Values)
            {
                if (!tracked.SpotId.Equals(spotId)) continue;
                generation = tracked.SpotGeneration;
                return true;
            }
        }

        generation = 0;
        return false;
    }

    internal async ValueTask ReleaseAsync(
        string meshName,
        string spotId,
        CancellationToken cancellationToken = default)
    {
        var key = new ZLinkSpotLocationKey(spotId);
        var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(key);
        TrackedSpot? tracked;
        lock (_gate)
        {
            if (!_spots.TryGetValue(canonical, out tracked))
            {
                return;
            }
        }

        var result = await runtime.RemoveSpotAsync(key, tracked.StoreGeneration, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status is not (ZLinkLocationWriteStatus.Stored or ZLinkLocationWriteStatus.IgnoredStale))
            throw new InvalidOperationException(
                $"SPOT '{spotId}' location release was rejected with '{result.Status}'.");

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

    // SpotGeneration is the spot's core lifecycle generation (row content);
    // StoreGeneration is the store-issued owner token presented on removal.
    private sealed record TrackedSpot(
        string SpotId,
        ulong SpotGeneration,
        ulong StoreGeneration,
        Func<CancellationToken, ValueTask>? Deactivate);
}
