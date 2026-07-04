package systems.zlink.framework.monitoring;

public enum ZLinkSocketEventKind {
    CONNECTED(0),
    CONNECTION_READY(1),
    DISCONNECTED(2),
    HANDSHAKE_FAILED(3),
    PEER_ADMISSION_CHANGED(4),
    CLOSED(5),
    INTERNAL(6);

    private final int value;

    ZLinkSocketEventKind(int value) { this.value = value; }

    public int value() { return value; }
}
