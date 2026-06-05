package systems.zlink.framework.registry;

import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceRole;

public record ZLinkRegistryServiceSummaryEntry(
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
