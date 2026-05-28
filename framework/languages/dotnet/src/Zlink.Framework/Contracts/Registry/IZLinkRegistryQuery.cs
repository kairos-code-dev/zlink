namespace Zlink.Framework.Contracts.Registry;

public interface IZLinkRegistryQuery
{
    ValueTask<ZLinkRegistryStatus> StatusAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummaryAsync(
        ZLinkRegistryServiceSummaryFilter? filter = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryTopologyEntry[]> TopologyAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRegistryQueryClient
{
    ValueTask<ZLinkRegistryTopologyEntry[]> TopologyAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRegistryQueryClientOptions
{
    string Endpoint { get; set; }
}
