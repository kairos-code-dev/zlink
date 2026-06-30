using System.Text;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotRouterEndpointDiscovery
{
    private const uint RouteKind = 0x5a4c5245;

    public static void BindLocalEndpoint(
        IZLinkBackendDiscovery discovery,
        RoutingId nodeRoutingId,
        string endpoint)
    {
        if (nodeRoutingId.IsEmpty || string.IsNullOrWhiteSpace(endpoint)) return;

        discovery.BindRoute(
            RouteKind,
            nodeRoutingId.ToBytes(),
            Encoding.UTF8.GetBytes(endpoint));
    }

    public static bool TryBindLocalEndpoint(
        IZLinkBackendDiscovery discovery,
        RoutingId nodeRoutingId,
        string endpoint)
    {
        try
        {
            BindLocalEndpoint(discovery, nodeRoutingId, endpoint);
            return true;
        }
        catch (ZlinkConfigException error)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"resolve-router-endpoint rid={nodeRoutingId.ToHex()} errno={error.NativeErrno}");
            return false;
        }
    }

    public static bool TryResolveEndpoint(
        IZLinkBackendDiscovery discovery,
        RoutingId nodeRoutingId,
        out string endpoint)
    {
        endpoint = string.Empty;
        if (nodeRoutingId.IsEmpty) return false;

        try
        {
            using var route = discovery.ResolveRoute(RouteKind, nodeRoutingId.ToBytes());
            endpoint = route.Value.GetString(Encoding.UTF8);
            return !string.IsNullOrWhiteSpace(endpoint);
        }
        catch (ZlinkConfigException)
        {
            return false;
        }
    }
}