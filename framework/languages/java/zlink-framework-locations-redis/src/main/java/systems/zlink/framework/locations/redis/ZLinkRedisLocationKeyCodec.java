package systems.zlink.framework.locations.redis;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

/*
 * Redis row key codec. It intentionally does not call the framework runtime
 * bookkeeping codec because Redis keys are a backend storage contract shared
 * across language implementations.
 */
final class ZLinkRedisLocationKeyCodec {
    private ZLinkRedisLocationKeyCodec() {
    }

    static String encodePeerKey(ZLinkPeerLocationKey key) {
        String identity = hasRid(key.nodeRid()) ? key.nodeRid().toHex() : nullToEmpty(key.endpoint());
        return encode(
            autoConnectTypeName(key.autoConnectType()),
            key.meshName(),
            roleName(key.role()),
            identity);
    }

    static String encodeSpotKey(ZLinkSpotLocationKey key) {
        return encode(key.meshName(), key.spotRid().toHex());
    }

    static String encodeActorKey(ZLinkActorLocationKey key) {
        return encode(key.actorId());
    }

    static String encodeRouteKey(ZLinkRouteLocationKey key) {
        return encode(Integer.toString(key.routeKind().value()), key.routeKey());
    }

    private static String encode(String... segments) {
        StringBuilder builder = new StringBuilder();
        for (String segment : segments) {
            String safe = nullToEmpty(segment);
            builder.append(safe.length()).append(':').append(safe);
        }
        return builder.toString();
    }

    private static boolean hasRid(RoutingId rid) {
        return rid != null && rid.size() > 0;
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }

    private static String autoConnectTypeName(ZLinkLocationAutoConnectType type) {
        return switch (type) {
            case ROUTE_MESH -> "route-mesh";
            case CLIENT_SERVER -> "client-server";
            case DEALER_MESH -> "dealer-mesh";
            case FANOUT -> "fanout";
            case SPOT_MESH -> "spot-mesh";
            default -> throw new IllegalArgumentException("invalid auto-connect type");
        };
    }

    private static String roleName(ZLinkLocationRole role) {
        return switch (role) {
            case ROUTER -> "router";
            case DEALER -> "dealer";
            case PUB -> "pub";
            case SUB -> "sub";
            case SPOT -> "spot";
            default -> throw new IllegalArgumentException("invalid location role");
        };
    }
}
