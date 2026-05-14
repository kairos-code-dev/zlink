namespace Zlink.Framework.AspNetCore;

internal sealed record ZLinkRegistryPollingSnapshot(
    ZLinkRegistryStatus Status,
    IReadOnlyList<ZLinkRegistryTopologyEntry> Topology,
    IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummary)
{
    public static async ValueTask<ZLinkRegistryPollingSnapshot> CaptureAsync(
        IZLinkRegistryQuery query,
        CancellationToken cancellationToken)
    {
        var status = await query.StatusSnapshotAsync(cancellationToken)
            .ConfigureAwait(false);
        var topology = (await query.TopologySnapshotAsync(cancellationToken)
                .ConfigureAwait(false))
            .OrderBy(static entry => entry.ChannelName, StringComparer.Ordinal)
            .ThenBy(static entry => entry.Endpoint, StringComparer.Ordinal)
            .ThenBy(static entry => entry.RoutingId?.ToString(), StringComparer.Ordinal)
            .ToArray();
        var summary = (await query.ServiceSummarySnapshotAsync(cancellationToken: cancellationToken)
                .ConfigureAwait(false))
            .OrderBy(static entry => entry.ChannelName, StringComparer.Ordinal)
            .ThenBy(static entry => entry.AutoConnectType)
            .ThenBy(static entry => entry.ServiceRole)
            .ToArray();

        return new ZLinkRegistryPollingSnapshot(status, topology, summary);
    }
}
