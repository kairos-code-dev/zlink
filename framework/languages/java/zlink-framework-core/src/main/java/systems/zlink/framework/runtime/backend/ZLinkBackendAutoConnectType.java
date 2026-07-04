package systems.zlink.framework.runtime.backend;

// Backend auto-connect is the channel/runtime subset of
// ZLinkLocationAutoConnectType. Dealer mesh is location-only.
public enum ZLinkBackendAutoConnectType {
    ROUTE_MESH,
    CLIENT_SERVER,
    FANOUT,
    SPOT_MESH
}
