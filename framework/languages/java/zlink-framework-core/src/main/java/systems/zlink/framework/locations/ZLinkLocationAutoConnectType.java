package systems.zlink.framework.locations;

public enum ZLinkLocationAutoConnectType {
    INVALID(0),
    ROUTE_MESH(1),
    CLIENT_SERVER(2),
    DEALER_MESH(3),
    FANOUT(4),
    SPOT_MESH(5);

    private final int value;

    ZLinkLocationAutoConnectType(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static ZLinkLocationAutoConnectType fromValue(int value) {
        return switch (value) {
            case 1 -> ROUTE_MESH;
            case 2 -> CLIENT_SERVER;
            case 3 -> DEALER_MESH;
            case 4 -> FANOUT;
            case 5 -> SPOT_MESH;
            default -> INVALID;
        };
    }
}
