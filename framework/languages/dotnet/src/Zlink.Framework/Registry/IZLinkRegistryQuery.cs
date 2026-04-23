namespace Zlink.Framework;

public interface IZLinkRegistryQuery
{
    ValueTask<ZLinkRegistryStatus> StatusSnapshotAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummarySnapshotAsync(
        ZLinkRegistryServiceSummaryFilter? filter = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryTopologyEntry[]> TopologySnapshotAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryTopologyEntry[]> TopologyQueryAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
        ZLinkServiceType serviceType,
        string serviceName,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRegistryQueryClient
{
    ValueTask<ZLinkRegistryTopologyEntry[]> SnapshotAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRegistryQueryClientOptions
{
    string Endpoint { get; set; }
}
