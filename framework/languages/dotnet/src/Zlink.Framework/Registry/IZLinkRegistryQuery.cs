namespace Zlink.Framework;

public interface IZLinkRegistryQuery
{
    ZLinkRegistryStatus StatusSnapshot();

    ZLinkRegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        ZLinkRegistryServiceSummaryFilter? filter = null);

    ZLinkRegistryTopologyEntry[] TopologySnapshot();

    ZLinkRegistryTopologyEntry[] TopologyQuery(
        ZLinkRegistryTopologyFilter? filter = null);

    ZLinkMemberPeerEntry[] MemberPeers(
        ZLinkServiceType serviceType,
        string serviceName);
}

public interface IZLinkRegistryQueryClient
{
    ZLinkRegistryTopologyEntry[] Snapshot(
        ZLinkRegistryTopologyFilter? filter = null);
}

public interface IZLinkRegistryQueryClientOptions
{
    string Endpoint { get; set; }
}
