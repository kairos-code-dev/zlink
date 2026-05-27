using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Registry;

internal static class ZLinkBackendDiscoveryFactory
{
    public static IZLinkBackendDiscovery Create(
        IZLinkChannelBackendAdapter adapter,
        IZLinkBackendContext context,
        string channelName,
        ZLinkAutoConnectType autoConnectType,
        IReadOnlyCollection<string> endpoints)
    {
        var discovery = adapter.CreateDiscovery(context, autoConnectType, channelName);
        foreach (var endpoint in endpoints)
        {
            discovery.ConnectRegistry(endpoint);
        }

        return discovery;
    }
}
