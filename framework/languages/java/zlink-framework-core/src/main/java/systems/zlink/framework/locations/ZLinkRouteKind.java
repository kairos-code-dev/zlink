package systems.zlink.framework.locations;

public enum ZLinkRouteKind {
    INVALID(0),
    ACTOR_SESSION(1),
    SPOT_NAME(2),
    FRAMEWORK_ROUTE(3);

    private final int value;

    ZLinkRouteKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static ZLinkRouteKind fromValue(int value) {
        return switch (value) {
            case 1 -> ACTOR_SESSION;
            case 2 -> SPOT_NAME;
            case 3 -> FRAMEWORK_ROUTE;
            default -> INVALID;
        };
    }
}
