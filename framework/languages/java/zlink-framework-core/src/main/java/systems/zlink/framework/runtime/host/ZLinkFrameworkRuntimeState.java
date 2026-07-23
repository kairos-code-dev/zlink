package systems.zlink.framework.runtime.host;

public enum ZLinkFrameworkRuntimeState {
    PREPARING(0),
    SERVING(1),
    RETIRING(2),
    DRAINING(3),
    STOPPED(4),
    ERROR(5);

    private final int wireValue;

    ZLinkFrameworkRuntimeState(int wireValue) {
        this.wireValue = wireValue;
    }

    public int wireValue() {
        return wireValue;
    }
}
