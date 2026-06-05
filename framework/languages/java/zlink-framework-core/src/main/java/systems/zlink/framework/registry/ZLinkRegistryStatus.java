package systems.zlink.framework.registry;

import systems.zlink.contracts.service.registry.RegistryState;

public record ZLinkRegistryStatus(
    int registryId,
    String bindEndpoint,
    RegistryState state,
    int topologyEntryCount,
    int peerRegistryCount,
    int connectedPeerRegistryCount,
    long listSeq,
    int lastError,
    long lastChangedMs) {
    public ZLinkRegistryStatus(RegistryState state, int topologyEntryCount) {
        this(0, "", state, topologyEntryCount, 0, 0, 0, 0, 0);
    }
}
