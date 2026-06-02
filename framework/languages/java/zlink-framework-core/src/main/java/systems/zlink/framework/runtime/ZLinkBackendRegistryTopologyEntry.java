package systems.zlink.framework.runtime;

public record ZLinkBackendRegistryTopologyEntry(
    String channelName,
    String serviceKind,
    String endpoint) {
}
