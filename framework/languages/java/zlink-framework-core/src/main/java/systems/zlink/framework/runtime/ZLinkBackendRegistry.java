package systems.zlink.framework.runtime;

import java.util.List;

public interface ZLinkBackendRegistry extends ZLinkBackendObject {
    void bind(String pubEndpoint, String routerEndpoint);

    void connectPeer(String pubEndpoint, String routerEndpoint);

    ZLinkBackendRegistryStatus status();

    List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter);

    List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter);
}
