using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Registry;

internal static class ZLinkBackendDiscoveryFactory
{
    private const int RetryErrno = 11;

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
            ConnectRegistryWithRetry(discovery, endpoint);
        }

        return discovery;
    }

    private static void ConnectRegistryWithRetry(
        IZLinkBackendDiscovery discovery,
        string endpoint)
    {
        for (var attempt = 0; ; attempt++)
        {
            try
            {
                discovery.ConnectRegistry(endpoint);
                return;
            }
            catch (ZlinkConfigException error) when (error.NativeErrno == RetryErrno && attempt < 20)
            {
                Thread.Sleep(50);
            }
        }
    }
}
