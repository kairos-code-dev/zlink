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
    IZLinkSpotAddressResolver,
    IZLinkActorAddressResolver
{
    private readonly IReadOnlyList<string> _meshNames;
    private readonly ZLinkStoreLocationResolvers _rows;

    internal ZLinkLocationAddressResolvers(
        ZLinkFrameworkRegistration registration,
        ZLinkStoreLocationResolvers rows)
    {
        _rows = rows;
        _meshNames = registration.SpotNodes.Values
            .Select(static node => node.SpotMeshChannelName ?? node.SpotNodeName)
            .Concat(registration.SpotMeshChannels.Keys)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
    }

    public async ValueTask<ZLinkSpotAddress?> ResolveSpotAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        foreach (var meshName in _meshNames)
        {
            var row = await _rows.ResolveSpotRowAsync(
                    new ZLinkSpotLocationKey(meshName, spotRid),
                    cancellationToken)
                .ConfigureAwait(false);
            if (row is not null)
            {
                return new ZLinkSpotAddress(row.NodeRid, row.SpotRid);
            }
        }

        return null;
    }

    public async ValueTask<ZLinkSpotAddress?> ResolveActorSpotAddressAsync(
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
            ? new ZLinkSpotAddress(row.NodeRid, row.NodeRid)
            : new ZLinkSpotAddress(row.NodeRid, row.SpotRid.Value);
    }
}
