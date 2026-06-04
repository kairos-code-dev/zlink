package systems.zlink.framework.registry;

public record ZLinkRegistryServiceSummaryEntry(
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
