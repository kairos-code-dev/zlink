namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    /// <summary>Starts the runtime when needed and exposes the started
    /// state so the location auto-connect host can wire its per-mesh loops
    /// to the created channel and spot node runtimes.</summary>
    internal ValueTask<ZLinkFrameworkRuntimeState> EnsureStartedStateAsync(
        CancellationToken cancellationToken) =>
        GetStartedStateAsync(cancellationToken);

    internal async ValueTask<ZLinkResolvedSpotHandle?> ResolveInstanceSpotHandleAsync(
        InstanceSpotAddress address,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(address);
        var store = Registration.Locations.ResolveStore() as IZLinkInstanceSpotLocationStore;
        if (store is null) return null;

        var key = new ZLinkSpotLocationKey(address.MeshName, address.SpotRid);
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
                row.SpotRid,
                row.SpotGeneration,
                ZLinkSpotKind.Instance),
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
                        location.SpotRid,
                        location.SpotGeneration,
                        ZLinkSpotKind.Instance),
                    location.LocationGeneration);
            });
    }
}
