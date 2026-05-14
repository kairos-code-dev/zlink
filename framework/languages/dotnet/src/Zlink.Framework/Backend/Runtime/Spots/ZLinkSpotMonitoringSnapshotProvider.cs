namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotMonitoringSnapshotProvider(IZLinkBackendSpotNode node)
{
    public ZLinkSpotMonitoringSnapshot Snapshot()
    {
        return new ZLinkSpotMonitoringSnapshot(
            node.StatusSnapshot(),
            node.PeersSnapshot()
                .OrderBy(static entry => entry.PeerEndpoint, StringComparer.Ordinal)
                .ThenBy(static entry => entry.ChannelName, StringComparer.Ordinal)
                .ToArray(),
            node.SubjectsSnapshot()
                .OrderBy(static entry => entry.Subject, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Role)
                .ToArray());
    }
}
