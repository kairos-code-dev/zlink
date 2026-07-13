package systems.zlink.framework.configuration;

/** Identifies the root that created a message flow. */
public enum ZLinkFlowOrigin {
    INBOUND,
    TIMER,
    APPLICATION,
    LIFECYCLE
}
