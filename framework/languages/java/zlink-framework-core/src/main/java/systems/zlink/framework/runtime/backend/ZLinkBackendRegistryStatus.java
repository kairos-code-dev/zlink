package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.service.registry.RegistryState;

public record ZLinkBackendRegistryStatus(
    int registryId,
    String bindEndpoint,
    RegistryState state,
    int topologyEntryCount,
    int peerRegistryCount,
    int connectedPeerRegistryCount,
    long listSeq,
    int lastError,
    long lastChangedMs) {
    public ZLinkBackendRegistryStatus(RegistryState state, int topologyEntryCount) {
        this(0, "", state, topologyEntryCount, 0, 0, 0, 0, 0);
    }
}
