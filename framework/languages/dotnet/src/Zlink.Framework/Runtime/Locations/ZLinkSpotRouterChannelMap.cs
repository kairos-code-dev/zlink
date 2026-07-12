namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkSpotRouterChannelMap(ZLinkLocationOptions options)
{
    internal string Resolve(string spotMeshName)
    {
        return options.SpotRouterChannels.TryGetValue(spotMeshName, out var routeChannelName)
            ? routeChannelName
            : spotMeshName;
    }
}
