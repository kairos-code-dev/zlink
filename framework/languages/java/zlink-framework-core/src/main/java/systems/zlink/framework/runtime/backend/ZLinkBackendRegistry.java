package systems.zlink.framework.runtime.backend;

import java.util.List;

public interface ZLinkBackendRegistry extends ZLinkBackendObject {
    void setId(int registryId);

    void bind(String pubEndpoint, String routerEndpoint);

    void connectPeer(String pubEndpoint, String routerEndpoint);

    ZLinkBackendRegistryStatus status();

    List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter);

    List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter);

    List<ZLinkBackendRegistryMemberPeerEntry> memberPeers(String channelName);
}
