namespace Zlink.Framework.Backend.Contracts;

internal interface IZLinkBackendRegistry : IZLinkBackendObject, IAsyncDisposable
{
    void SetId(uint registryId);

    void SetHeartbeat(uint intervalMs, uint timeoutMs);

    void SetBroadcastInterval(uint intervalMs);

    void AddPeer(string endpoint);

    void Bind(string pubEndpoint, string routerEndpoint);

    ZLinkRegistryStatus StatusSnapshot();

    IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummarySnapshot(
        ZLinkRegistryServiceSummaryFilter? filter);

    IReadOnlyList<ZLinkRegistryTopologyEntry> TopologySnapshot();

    IReadOnlyList<ZLinkRegistryTopologyEntry> TopologyQuery(
        ZLinkRegistryTopologyFilter? filter);

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers(string channelName);
}

internal interface IZLinkBackendRegistryQueryClient : IZLinkBackendObject, IAsyncDisposable
{
    void Connect(string endpoint);

    IReadOnlyList<ZLinkRegistryTopologyEntry> Snapshot(
        ZLinkRegistryTopologyFilter? filter);
}
