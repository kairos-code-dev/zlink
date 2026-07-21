package systems.zlink.framework.channels;

public enum ZLinkSubmitStatus {
    SUBMITTED,
    BACKPRESSURED,
    TIMED_OUT,
    TARGET_NOT_FOUND,
    ROUTE_NOT_CONNECTED,
    SHUTDOWN
}
