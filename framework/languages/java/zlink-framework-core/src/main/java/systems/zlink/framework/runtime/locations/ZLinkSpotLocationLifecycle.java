package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

final class ZLinkSpotLocationLifecycle {
    private final ZLinkLocationRuntime runtime;
    private final Object gate = new Object();
    private final Map<String, TrackedSpot> spots = new HashMap<>();

    ZLinkSpotLocationLifecycle(ZLinkLocationRuntime runtime) {
        this.runtime = runtime;
    }

    CompletionStage<ZLinkLocationWriteStatus> claim(
        String meshName,
        RoutingId spotRid,
        long spotGeneration,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        Runnable deactivate) {
        ZLinkSpotLocation row = new ZLinkSpotLocation(
            meshName,
            spotRid,
            spotGeneration,
            spotType,
            nodeRid,
            spotKind,
            routeEndpoint,
            "",
            0,
            Instant.EPOCH);
        return runtime.writeSpot(row, ZLinkLocationWriteIntent.NEW_CLAIM)
            .thenApply(result -> {
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    String key = ZLinkLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(meshName, spotRid));
                    synchronized (gate) {
                        spots.put(key, new TrackedSpot(result.generation(), deactivate));
                    }
                }
                return result.status();
            });
    }

    CompletionStage<Void> release(String meshName, RoutingId spotRid) {
        ZLinkSpotLocationKey key = new ZLinkSpotLocationKey(meshName, spotRid);
        String canonical = ZLinkLocationKeyCodec.encodeSpotKey(key);
        TrackedSpot tracked;
        synchronized (gate) {
            tracked = spots.remove(canonical);
        }
        if (tracked == null) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.removeSpot(key, tracked.generation()).thenApply(ignored -> null);
    }

    void onOwnershipLost(String canonicalKey) {
        Runnable deactivate = null;
        synchronized (gate) {
            TrackedSpot spot = spots.remove(canonicalKey);
            if (spot != null) {
                deactivate = spot.deactivate();
            }
        }
        if (deactivate != null) {
            deactivate.run();
        }
    }

    private record TrackedSpot(long generation, Runnable deactivate) {
    }
}
