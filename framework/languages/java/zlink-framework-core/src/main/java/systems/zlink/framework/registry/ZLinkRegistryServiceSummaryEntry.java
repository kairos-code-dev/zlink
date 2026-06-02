package systems.zlink.framework.registry;

public record ZLinkRegistryServiceSummaryEntry(
    String channelName,
    String serviceKind,
    int serviceCount) {
}
