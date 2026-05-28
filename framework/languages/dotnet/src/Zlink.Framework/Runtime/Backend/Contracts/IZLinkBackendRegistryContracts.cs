namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkBackendRegistry : IZLinkBackendObject, IAsyncDisposable
{
    void SetId(uint registryId);

    void SetHeartbeat(uint intervalMs, uint timeoutMs);

    void SetBroadcastInterval(uint intervalMs);

    void AddPeer(string endpoint);

    void Bind(string pubEndpoint, string routerEndpoint);

    ZLinkRegistryStatus Status();

    IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummary(
        ZLinkRegistryServiceSummaryFilter? filter);

    IReadOnlyList<ZLinkRegistryTopologyEntry> Topology();

    IReadOnlyList<ZLinkRegistryTopologyEntry> Topology(
        ZLinkRegistryTopologyFilter? filter);

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers(string channelName);
}

internal interface IZLinkBackendRegistryQueryClient : IZLinkBackendObject, IAsyncDisposable
{
    void Connect(string endpoint);

    IReadOnlyList<ZLinkRegistryTopologyEntry> Topology(
        ZLinkRegistryTopologyFilter? filter);
}
