package systems.zlink.framework.runtime;

public record ZLinkBackendRegistryServiceSummaryEntry(
    String channelName,
    String serviceKind,
    int serviceCount) {
}
