package systems.zlink.framework.runtime.locations;

import java.util.HexFormat;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationCanonicalNames;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

final class ZLinkLocationKeyCodec {
    private static final HexFormat HEX = HexFormat.of();

    private ZLinkLocationKeyCodec() {
    }

    static String encodePeerKey(ZLinkPeerLocationKey key) {
        String identity = key.nodeRid() != null ? toHex(key.nodeRid()) : nullToEmpty(key.endpoint());
        return encode(
            ZLinkLocationCanonicalNames.toCanonicalString(key.autoConnectType()),
            key.meshName(),
            ZLinkLocationCanonicalNames.toCanonicalString(key.role()),
            identity);
    }

    static String encodeSpotKey(ZLinkSpotLocationKey key) {
        return encode(key.meshName(), toHex(key.spotRid()));
    }

    static String encodeActorKey(ZLinkActorLocationKey key) {
        return encode(normalizeActorType(key.actorType()), key.actorId());
    }

    static String encodeRouteKey(ZLinkRouteLocationKey key) {
        return encode(Integer.toString(key.routeKind().ordinal()), key.routeKey());
    }

    static String normalizeActorType(String actorType) {
        return nullToEmpty(actorType);
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
}
