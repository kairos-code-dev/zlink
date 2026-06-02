package systems.zlink.framework.runtime;

import java.util.List;

public interface ZLinkBackendRegistryQueryClient extends ZLinkBackendObject {
    void connect(String endpoint);

    List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter);

    List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter);
}
