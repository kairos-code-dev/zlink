namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Public messaging lookup surfaces: spot rid / actor id to a full spot
/// address. A spot rid is searched across every spot mesh registered on
/// this node (the same names the auto-connect host advertises under); the
/// actor lookup derives the address from the actor row's spot kind — the
/// entry spot address is the node itself. Callers hold the address and
/// re-resolve on failure; nothing here caches.
/// </summary>
internal sealed class ZLinkLocationAddressResolvers :
    IZLinkSpotRefResolver,
    IZLinkActorAddressResolver
{
    private readonly ZLinkStoreLocationResolvers _rows;
    private readonly ZLinkSpotMeshLocationResolver _spots;

    internal ZLinkLocationAddressResolvers(
        ZLinkStoreLocationResolvers rows,
        ZLinkSpotMeshLocationResolver spots)
    {
        _rows = rows;
        _spots = spots;
    }

    public async ValueTask<SpotRef?> ResolveSpotRefAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        var row = await _spots.ResolveAsync(spotRid, cancellationToken).ConfigureAwait(false);
        return row is null ? null : new SpotRef(row.NodeRid, row.SpotRid);
    }

    public async ValueTask<SpotRef?> ResolveActorSpotRefAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var row = await _rows.ResolveActorRowAsync(
                new ZLinkActorLocationKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (row is null)
        {
            return null;
        }

        // An entry spot's address is the node itself (draft §4); a user
        // spot actor addresses its user spot.
        return row.LocationKind == ZLinkSpotKind.Entry || row.SpotRid is not { Size: > 0 }
            ? new SpotRef(row.NodeRid, row.NodeRid)
            : new SpotRef(row.NodeRid, row.SpotRid.Value);
    }
}
