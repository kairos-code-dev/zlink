package systems.zlink.framework.runtime.backend;

public record ZLinkBackendRegistryStatus(
    int registryId,
    String bindEndpoint,
    String state,
    int topologyEntryCount,
    int peerRegistryCount,
    int connectedPeerRegistryCount,
    long listSeq,
    int lastError,
    long lastChangedMs) {
    public ZLinkBackendRegistryStatus(String state, int topologyEntryCount) {
        this(0, "", state, topologyEntryCount, 0, 0, 0, 0, 0);
    }
}
