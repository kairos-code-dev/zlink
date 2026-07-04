package systems.zlink.framework.errors;

public enum ZLinkFrameworkErrorKind {
    ACTOR_ROUTE_NOT_FOUND(0),
    ACTOR_CREATE_FAILED(1),
    ACTOR_ALREADY_EXISTS(2),
    ACTOR_TYPE_MISMATCH(3),
    SPOT_CREATE_FAILED(4),
    SPOT_ROUTE_NOT_FOUND(5),
    SPOT_TYPE_MISMATCH(6),
    ACTOR_SESSION_NOT_BOUND(7),
    HANDLER_NOT_FOUND(8),
    ROUTE_HANDLER_NOT_FOUND(9),
    ACTOR_DISPATCH_HANDLER_NOT_FOUND(10),
    PAYLOAD_DECODE_FAILED(11),
    ROUTE_NOT_CONNECTED(12),
    REQUEST_TARGET_NOT_FOUND(13),
    REQUEST_REJECTED(14),
    REQUEST_PROTOCOL_ERROR(15),
    REQUEST_FAILED(16),
    WORKER_QUEUE_FULL(17),
    WORKER_TIMED_OUT(18),
    WORKER_FAILED(19),
    ACTOR_LOCATION_STALE(20),
    ACTOR_CREATE_REJECTED(21);

    private final int value;

    ZLinkFrameworkErrorKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public boolean retriable() {
        return this == ROUTE_NOT_CONNECTED || this == ACTOR_LOCATION_STALE;
    }

    public static ZLinkFrameworkErrorKind fromValue(int value) {
        for (ZLinkFrameworkErrorKind kind : values()) {
            if (kind.value == value) {
                return kind;
            }
        }
        throw new IllegalArgumentException("unknown framework error kind: " + value);
    }
}
