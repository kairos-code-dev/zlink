package systems.zlink.framework.runtime.backend;

public record ZLinkBackendRegistryTopologyEntry(
    String channelName,
    String serviceKind,
    String endpoint) {
}
