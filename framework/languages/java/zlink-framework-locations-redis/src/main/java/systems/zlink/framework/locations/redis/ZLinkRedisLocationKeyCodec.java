package systems.zlink.framework.locations.redis;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationCanonicalNames;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

final class ZLinkRedisLocationKeyCodec {
    private ZLinkRedisLocationKeyCodec() {
    }

    static String encodePeerKey(ZLinkPeerLocationKey key) {
        String identity = hasRid(key.nodeRid()) ? key.nodeRid().toHex() : nullToEmpty(key.endpoint());
        return encode(
            ZLinkLocationCanonicalNames.toCanonicalString(key.autoConnectType()),
            key.meshName(),
            ZLinkLocationCanonicalNames.toCanonicalString(key.role()),
            identity);
    }

    static String encodeSpotKey(ZLinkSpotLocationKey key) {
        return encode(key.meshName(), key.spotRid().toHex());
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
}
