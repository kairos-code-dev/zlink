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
        string? routeEndpoint,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken = default)
    {
        var row = new ZLinkSpotLocation(
            meshName,
            spotRid,
            spotType,
            nodeRid,
            spotKind,
            routeEndpoint,
            OwnerId: string.Empty,
            Generation: 0,
            UpdatedAt: default);
        var result = await runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
            .ConfigureAwait(false);
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict)
        {
            var existing = await resolver.ResolveSpotRowAsync(
                new ZLinkSpotLocationKey(meshName, spotRid),
                cancellationToken).ConfigureAwait(false);
            if (existing?.NodeRid == nodeRid)
            {
                result = await runtime.WriteSpotAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        if (result.Status == ZLinkLocationWriteStatus.Stored)
        {
            var canonical = ZLinkLocationKeyCodec.EncodeSpotKey(
                new ZLinkSpotLocationKey(meshName, spotRid));
            lock (_gate)
            {
                _spots[canonical] = new TrackedSpot(result.Generation, deactivate);
            }
        }

        return result.Status;
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
            if (!_spots.Remove(canonical, out tracked))
            {
                return;
            }
        }

        await runtime.RemoveSpotAsync(key, tracked.Generation, cancellationToken).ConfigureAwait(false);
    }

    internal Func<CancellationToken, ValueTask>? TakeOwnershipLostDeactivation(string canonicalKey)
    {
        lock (_gate)
        {
            return _spots.Remove(canonicalKey, out var spot) ? spot.Deactivate : null;
        }
    }

    private sealed record TrackedSpot(
        long Generation,
        Func<CancellationToken, ValueTask>? Deactivate);
}
