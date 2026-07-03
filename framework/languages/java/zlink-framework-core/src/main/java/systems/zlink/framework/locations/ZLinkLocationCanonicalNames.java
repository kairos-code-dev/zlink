package systems.zlink.framework.locations;

public final class ZLinkLocationCanonicalNames {
    private ZLinkLocationCanonicalNames() {
    }

    public static String toCanonicalString(ZLinkLocationAutoConnectType type) {
        return switch (type) {
            case ROUTE_MESH -> "route-mesh";
            case CLIENT_SERVER -> "client-server";
            case DEALER_MESH -> "dealer-mesh";
            case FANOUT -> "fanout";
            case SPOT_MESH -> "spot-mesh";
            default -> throw new IllegalArgumentException("Unknown auto-connect type: " + type);
        };
    }

    public static String toCanonicalString(ZLinkLocationRole role) {
        return switch (role) {
            case ROUTER -> "router";
            case DEALER -> "dealer";
            case PUB -> "pub";
            case SUB -> "sub";
            case SPOT -> "spot";
            default -> throw new IllegalArgumentException("Unknown location role: " + role);
        };
    }

    public static ZLinkLocationAutoConnectType parseAutoConnectType(String value) {
        return switch (value) {
            case "route-mesh" -> ZLinkLocationAutoConnectType.ROUTE_MESH;
            case "client-server" -> ZLinkLocationAutoConnectType.CLIENT_SERVER;
            case "dealer-mesh" -> ZLinkLocationAutoConnectType.DEALER_MESH;
            case "fanout" -> ZLinkLocationAutoConnectType.FANOUT;
            case "spot-mesh" -> ZLinkLocationAutoConnectType.SPOT_MESH;
            default -> ZLinkLocationAutoConnectType.INVALID;
        };
    }

    public static ZLinkLocationRole parseRole(String value) {
        return switch (value) {
            case "router" -> ZLinkLocationRole.ROUTER;
            case "dealer" -> ZLinkLocationRole.DEALER;
            case "pub" -> ZLinkLocationRole.PUB;
            case "sub" -> ZLinkLocationRole.SUB;
            case "spot" -> ZLinkLocationRole.SPOT;
            default -> ZLinkLocationRole.INVALID;
        };
    }
}
