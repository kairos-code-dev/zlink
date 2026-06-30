namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistrySpotRemoteAddressResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSpotRemoteAddressResolver
{
    public async ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistrySpotRemoteAddresses
                      ?? throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var discovery = ZLinkRegistryRouteRuntime.ResolveSpotDiscovery(
            state,
            routerChannelId);

        try
        {
            var spotRoute = discovery.ResolveSpot(spotRid);
            return new ZLinkSpotRemoteAddress(
                routerChannelId,
                spotRoute.OwnerNodeRid,
                spotRoute.SpotRid,
                spotRoute.SpotKind.ToFramework());
        }
        catch (ZlinkConfigException error) when (error.NativeErrno == 2)
        {
            throw NotFound(spotRid.ToHex(), error);
        }
    }

    private static ZLinkFrameworkException NotFound(
        string identity,
        Exception? inner = null)
    {
        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotRouteNotFound,
            $"SPOT route was not found for '{identity}'.",
            true,
            inner);
    }
}