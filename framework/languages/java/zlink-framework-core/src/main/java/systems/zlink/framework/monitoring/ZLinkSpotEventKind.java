package systems.zlink.framework.monitoring;

public enum ZLinkSpotEventKind {
    STATUS_CHANGED(0),
    PEERS_CHANGED(1),
    SUBJECTS_CHANGED(2),
    TIMER_HANDLER_FAILED(3),
    TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION(4);

    private final int value;

    ZLinkSpotEventKind(int value) { this.value = value; }

    public int value() { return value; }
}
