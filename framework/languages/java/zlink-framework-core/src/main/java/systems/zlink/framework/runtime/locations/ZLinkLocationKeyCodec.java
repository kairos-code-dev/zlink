package systems.zlink.framework.runtime.locations;

import java.util.HexFormat;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

/*
 * Framework-internal row key codec for in-memory bookkeeping and runtime
 * generation tracking. Backend extensions own their transport key codec, so
 * Redis keys are encoded in zlink-framework-locations-redis instead of calling
 * through this type.
 */
final class ZLinkLocationKeyCodec {
    private static final HexFormat HEX = HexFormat.of();

    private ZLinkLocationKeyCodec() {
    }

    static String encodePeerKey(ZLinkPeerLocationKey key) {
        String identity = key.nodeRid() != null ? toHex(key.nodeRid()) : nullToEmpty(key.endpoint());
        return encode(
            autoConnectTypeName(key.autoConnectType()),
            key.meshName(),
            roleName(key.role()),
            identity);
    }

    static String encodeSpotKey(ZLinkSpotLocationKey key) {
        return encode(key.meshName(), toHex(key.spotRid()));
    }

    static String encodeActorKey(ZLinkActorLocationKey key) {
        return encode(key.actorId());
    }

    static String encodeRouteKey(ZLinkRouteLocationKey key) {
        return encode(Integer.toString(key.routeKind().value()), key.routeKey());
    }

    static String encodeFanoutPublisherKey(
        ZLinkFanoutPublisherDescriptorKey key) {
        return encode(key.channelName(), toHex(key.publisherRid()));
    }

    private static String encode(String... segments) {
        StringBuilder builder = new StringBuilder();
        for (String segment : segments) {
            String safeSegment = nullToEmpty(segment);
            builder.append(safeSegment.length()).append(':').append(safeSegment);
        }
        return builder.toString();
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }

    private static String toHex(RoutingId routingId) {
        return HEX.formatHex(routingId.toBytes());
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
