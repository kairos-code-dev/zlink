namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkSpotMeshLocationResolver
{
    private readonly IReadOnlyList<string> _meshNames;
    private readonly ZLinkStoreLocationResolvers _rows;

    internal ZLinkSpotMeshLocationResolver(
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

    internal async ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        foreach (var meshName in _meshNames)
        {
            var row = await _rows.ResolveActorRowAsync(
                    new ZLinkActorLocationKey(meshName, actorId),
                    cancellationToken)
                .ConfigureAwait(false);
            if (row is not null) return row;
        }

        return null;
    }

    internal async ValueTask<ZLinkSpotLocation?> ResolveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var meshName in _meshNames)
        {
            var row = await _rows.ResolveSpotRowAsync(
                    new ZLinkSpotLocationKey(meshName, spotRid),
                    cancellationToken)
                .ConfigureAwait(false);
            if (row is not null) return row;
        }

        return null;
    }
}
