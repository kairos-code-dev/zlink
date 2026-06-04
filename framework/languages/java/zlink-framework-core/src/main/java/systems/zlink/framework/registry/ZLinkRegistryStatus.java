package systems.zlink.framework.registry;

public record ZLinkRegistryStatus(
    int registryId,
    String bindEndpoint,
    String state,
    int topologyEntryCount,
    int peerRegistryCount,
    int connectedPeerRegistryCount,
    long listSeq,
    int lastError,
    long lastChangedMs) {
    public ZLinkRegistryStatus(String state, int topologyEntryCount) {
        this(0, "", state, topologyEntryCount, 0, 0, 0, 0, 0);
    }
}
