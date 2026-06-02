package systems.zlink.framework.registry;

public record ZLinkRegistryTopologyEntry(
    String channelName,
    String serviceKind,
    String endpoint) {
}
