namespace Zlink.Framework.Configuration;

internal enum ZLinkPeerAcquisitionMode
{
    None,
    Discovery,
    Manual,
}

internal static class ZLinkPeerAcquisitionPolicy
{
    public static ZLinkPeerAcquisitionMode Resolve(
        bool discoveryConfigured,
        IReadOnlyCollection<string> manualConnections)
    {
        if (manualConnections.Count > 0)
        {
            return ZLinkPeerAcquisitionMode.Manual;
        }

        return discoveryConfigured
            ? ZLinkPeerAcquisitionMode.Discovery
            : ZLinkPeerAcquisitionMode.None;
    }

    public static void RequirePeerSource(
        string capabilityName,
        bool discoveryConfigured,
        IReadOnlyCollection<string> manualConnections)
    {
        if (Resolve(discoveryConfigured, manualConnections) == ZLinkPeerAcquisitionMode.None)
        {
            throw new ZLinkConfigurationException(
                $"{capabilityName} requires discovery or manual connections.");
        }
    }

    public static void RequireSinglePeerSource(
        string capabilityName,
        bool discoveryConfigured,
        IReadOnlyCollection<string> manualConnections)
    {
        if (discoveryConfigured && manualConnections.Count > 0)
        {
            throw new ZLinkConfigurationException(
                $"{capabilityName} cannot mix discovery and manual connections.");
        }

        RequirePeerSource(capabilityName, discoveryConfigured, manualConnections);
    }
}
