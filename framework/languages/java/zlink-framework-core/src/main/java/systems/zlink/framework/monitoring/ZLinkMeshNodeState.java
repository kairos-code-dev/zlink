package systems.zlink.framework.monitoring;

public enum ZLinkMeshNodeState {
    STARTING,
    SERVING,
    DRAINING,
    DRAINED,
    FORCE_STOPPING,
    STOPPED,
    FAULTED
}
