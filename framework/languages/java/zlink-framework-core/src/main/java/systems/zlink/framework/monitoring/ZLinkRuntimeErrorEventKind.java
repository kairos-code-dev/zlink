package systems.zlink.framework.monitoring;

public enum ZLinkRuntimeErrorEventKind {
    MESSAGE_FLOW_OBSERVER_FAILED(0);

    private final int value;

    ZLinkRuntimeErrorEventKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }
}
