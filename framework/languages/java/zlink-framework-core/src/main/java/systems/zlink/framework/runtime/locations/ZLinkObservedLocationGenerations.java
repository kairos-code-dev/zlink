package systems.zlink.framework.runtime.locations;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

final class ZLinkObservedLocationGenerations {
    private final ConcurrentMap<String, Long> generations = new ConcurrentHashMap<>();

    boolean acceptPeer(ZLinkPeerLocation row) {
        return accept(
            ZLinkLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
                row.autoConnectType(),
                row.meshName(),
                row.role(),
                row.nodeRid(),
                row.endpoint())),
            row.generation());
    }

    boolean acceptSpot(ZLinkSpotLocation row) {
        return accept(
            ZLinkLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(row.spotId())),
            row.generation());
    }

    boolean acceptActor(ZLinkActorLocation row) {
        return accept(
            ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(
                row.actorId())),
            row.generation());
    }

    boolean acceptRoute(ZLinkRouteLocation row) {
        return accept(
            ZLinkLocationKeyCodec.encodeRouteKey(new ZLinkRouteLocationKey(
                row.routeKind(),
                row.routeKey())),
            row.generation());
    }

    private boolean accept(String key, long generation) {
        Long previous = generations.get(key);
        if (previous != null && generation < previous) {
            return false;
        }
        generations.merge(key, generation, Math::max);
        return true;
    }
}
