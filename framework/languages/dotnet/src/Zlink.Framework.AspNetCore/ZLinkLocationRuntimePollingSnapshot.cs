namespace Zlink.Framework.AspNetCore;

internal sealed record ZLinkLocationRuntimePollingSnapshot(
    ZLinkLocationRuntimeStatus Status,
    IReadOnlyList<ZLinkLocationTopologyEntry> Topology,
    IReadOnlyList<ZLinkLocationServiceSummary> ServiceSummary)
{
    public static async ValueTask<ZLinkLocationRuntimePollingSnapshot> CaptureAsync(
        IZLinkLocationRuntimeQuery query,
        CancellationToken cancellationToken)
    {
        var status = await query.GetStatusAsync(cancellationToken).ConfigureAwait(false);

        var topology = new List<ZLinkLocationTopologyEntry>();
        var page = new ZLinkPageRequest();
        while (true)
        {
            var result = await query.ListTopologyAsync(
                    new ZLinkLocationTopologyFilter(),
                    page,
                    cancellationToken)
                .ConfigureAwait(false);
            topology.AddRange(result.Items);
            if (result.ContinuationToken is not { } token) break;

            page = new ZLinkPageRequest(ContinuationToken: token);
        }

        var summary = await query.ListServiceSummariesAsync(
                new ZLinkLocationServiceSummaryFilter(),
                cancellationToken)
            .ConfigureAwait(false);

        return new ZLinkLocationRuntimePollingSnapshot(
            status,
            topology
                .OrderBy(static entry => entry.MeshName, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Endpoint, StringComparer.Ordinal)
                .ThenBy(static entry => entry.NodeRid.ToString(), StringComparer.Ordinal)
                .ToArray(),
            summary
                .OrderBy(static entry => entry.MeshName, StringComparer.Ordinal)
                .ToArray());
    }
}
