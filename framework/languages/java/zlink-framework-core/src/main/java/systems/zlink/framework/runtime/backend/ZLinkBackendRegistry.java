package systems.zlink.framework.runtime.backend;

import java.time.Duration;
import java.util.List;

public interface ZLinkBackendRegistry extends ZLinkBackendObject {
    void setId(int registryId);

    void setHeartbeat(Duration interval, Duration timeout);

    void setBroadcastInterval(Duration interval);

    void bind(String pubEndpoint, String routerEndpoint);

    void connectPeer(String pubEndpoint, String routerEndpoint);

    ZLinkBackendRegistryStatus status();

    List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter);

    List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter);

    List<ZLinkBackendRegistryMemberPeerEntry> memberPeers(String channelName);
}
