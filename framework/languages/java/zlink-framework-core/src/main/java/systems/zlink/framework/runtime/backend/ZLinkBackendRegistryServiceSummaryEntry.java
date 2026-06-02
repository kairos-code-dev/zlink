package systems.zlink.framework.runtime.backend;

public record ZLinkBackendRegistryServiceSummaryEntry(
    String channelName,
    String serviceKind,
    int serviceCount) {
}
