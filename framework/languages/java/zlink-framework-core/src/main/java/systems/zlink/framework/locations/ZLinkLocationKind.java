package systems.zlink.framework.locations;

public enum ZLinkLocationKind {
    INVALID(0),
    PEER(1),
    SPOT(2),
    ACTOR(3),
    ROUTE(4);

    private final int value;

    ZLinkLocationKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static ZLinkLocationKind fromValue(int value) {
        return switch (value) {
            case 1 -> PEER;
            case 2 -> SPOT;
            case 3 -> ACTOR;
            case 4 -> ROUTE;
            default -> INVALID;
        };
    }
}
