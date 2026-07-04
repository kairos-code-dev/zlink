package systems.zlink.framework.monitoring;

public enum ZLinkLocationRuntimeEventKind {
    STATUS_CHANGED(0),
    TOPOLOGY_CHANGED(1),
    SERVICE_SUMMARY_CHANGED(2),
    STORE_FAILURE(3),
    STORE_RECOVERED(4);

    private final int value;

    ZLinkLocationRuntimeEventKind(int value) { this.value = value; }

    public int value() { return value; }
}
