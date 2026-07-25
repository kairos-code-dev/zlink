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
    ACTOR_CREATE_REJECTED(21),
    OBJECT_CLIENT_NOT_CONFIGURED(22),
    MESH_SELECTION_REQUIRED(23),
    MESH_NOT_FOUND(24),
    INVALID_CONFIGURATION(25),
    ALREADY_SUBMITTED(26),
    ACTOR_GENERATION_STALE(27),
    ACTOR_MOVING(28),
    DEADLINE_EXCEEDED(29),
    PLACEMENT_CAPACITY_EXHAUSTED(30),
    ROUTING_ID_CONFLICT(31),
    SPOT_GENERATION_STALE(32),
    SPOT_MOVING(33),
    RELOCATION_DATA_LOST(34),
    SPOT_ID_CONFLICT(35),
    RUNTIME_SHUTDOWN(36),
    RELOCATION_DISABLED(37),
    RELOCATION_TARGET_UNAVAILABLE(38),
    RELOCATION_FAILED(39);

    private final int value;

    ZLinkFrameworkErrorKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    /**
     * Default retry classification fixed by the public error kind table in
     * {@code framework/doc/framework/common/spec/05-framework-api.ko.md} §13. Retry policy stays
     * with the kind so callers never need a per-surface table.
     */
    public boolean retriable() {
        return switch (this) {
            case ROUTE_NOT_CONNECTED,
                ACTOR_LOCATION_STALE,
                ACTOR_MOVING,
                DEADLINE_EXCEEDED,
                PLACEMENT_CAPACITY_EXHAUSTED,
                SPOT_MOVING,
                RELOCATION_TARGET_UNAVAILABLE,
                RELOCATION_FAILED -> true;
            default -> false;
        };
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
