package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceRole;

public record ZLinkBackendRegistryServiceSummaryEntry(
    AutoConnectType autoConnectType,
    ServiceRole serviceRole,
    String channelName,
    int totalCount,
    int connectingCount,
    int readyCount,
    int errorCount,
    int stoppedCount,
    long lastReportedMs) {
}
