package systems.zlink.framework.runtime.backend;

public record ZLinkBackendRegistryServiceSummaryEntry(
    String autoConnectType,
    String serviceRole,
    String channelName,
    int totalCount,
    int connectingCount,
    int readyCount,
    int errorCount,
    int stoppedCount,
    long lastReportedMs) {
}
