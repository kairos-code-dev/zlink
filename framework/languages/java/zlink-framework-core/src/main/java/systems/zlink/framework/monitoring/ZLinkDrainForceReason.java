package systems.zlink.framework.monitoring;

public enum ZLinkDrainForceReason {
    DEADLINE_EXCEEDED,
    DRAINING_STATE_PUBLISH_FAILED,
    OWNER_CLEANUP_FAILED,
    TEARDOWN_FAILED
}
