package systems.zlink.framework.runtime.locations;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;

final class ZLinkActorSessionRouteLifecycle {
    private final ZLinkLocationRuntime runtime;
    private final Object gate = new Object();
    private final Map<String, Long> routes = new HashMap<>();

    ZLinkActorSessionRouteLifecycle(ZLinkLocationRuntime runtime) {
        this.runtime = runtime;
    }

    CompletionStage<Void> bind(
        RoutingId sessionRid,
        String actorId,
        RoutingId ownerNodeRid) {
        String routeKey = toRouteKey(sessionRid);
        ZLinkRouteLocation row = new ZLinkRouteLocation(
            ZLinkRouteKind.ACTOR_SESSION,
            routeKey,
            ownerNodeRid,
            "",
            0,
            actorId.getBytes(StandardCharsets.UTF_8),
            Instant.EPOCH);
        return runtime.writeRoute(row, ZLinkLocationWriteIntent.NEW_CLAIM)
            .thenCompose(result -> {
                if (result.status() == ZLinkLocationWriteStatus.REJECTED_CONFLICT) {
                    return runtime.writeRoute(row, ZLinkLocationWriteIntent.TAKEOVER);
                }
                return CompletableFuture.completedFuture(result);
            })
            .thenApply(result -> {
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    String key = ZLinkLocationKeyCodec.encodeRouteKey(
                        new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, routeKey));
                    synchronized (gate) {
                        routes.put(key, result.generation());
                    }
                }
                return null;
            });
    }

    CompletionStage<Void> remove(RoutingId sessionRid) {
        ZLinkRouteLocationKey key = new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, toRouteKey(sessionRid));
        String canonical = ZLinkLocationKeyCodec.encodeRouteKey(key);
        Long generation;
        synchronized (gate) {
            generation = routes.remove(canonical);
        }
        if (generation == null) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.removeRoute(key, generation).thenApply(ignored -> null);
    }

    void onOwnershipLost(String canonicalKey) {
        synchronized (gate) {
            routes.remove(canonicalKey);
        }
    }

    private static String toRouteKey(RoutingId routingId) {
        return java.util.HexFormat.of().formatHex(routingId.toBytes());
    }
}
