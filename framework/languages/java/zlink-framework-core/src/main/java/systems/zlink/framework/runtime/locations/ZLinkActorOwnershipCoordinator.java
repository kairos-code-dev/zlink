package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.spots.ZLinkSpotKind;

final class ZLinkActorOwnershipCoordinator {
    private final ZLinkLocationRuntime runtime;
    private final Object gate = new Object();
    private final Map<String, TrackedActor> actors = new HashMap<>();

    ZLinkActorOwnershipCoordinator(ZLinkLocationRuntime runtime) {
        this.runtime = runtime;
    }

    CompletionStage<ZLinkLocationWriteStatus> claim(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return write(actorType, actorId, nodeRid, deactivate, ZLinkLocationWriteIntent.NEW_CLAIM);
    }

    CompletionStage<ZLinkLocationWriteStatus> takeover(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return write(actorType, actorId, nodeRid, deactivate, ZLinkLocationWriteIntent.TAKEOVER);
    }

    CompletionStage<Void> setActorRef(String actorType, String actorId, ActorRef actorRef) {
        return renew(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorId(), row.actorType(), actorRef, row.nodeRid(), row.locationKind(),
            row.spotMeshName(), row.spotRid(), row.ownerId(), row.generation(), row.updatedAt()));
    }

    CompletionStage<Void> notifyJoinedSpot(
        String actorType,
        String actorId,
        String meshName,
        RoutingId spotRid) {
        return renew(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorId(), row.actorType(), row.actorRef(), row.nodeRid(), ZLinkSpotKind.USER,
            meshName, spotRid, row.ownerId(), row.generation(), row.updatedAt()));
    }

    CompletionStage<Void> notifyLeftSpot(String actorType, String actorId) {
        return renew(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorId(), row.actorType(), row.actorRef(), row.nodeRid(), ZLinkSpotKind.ENTRY,
            "", null, row.ownerId(), row.generation(), row.updatedAt()));
    }

    CompletionStage<Void> notifyMovedToEntrySpot(
        String actorType,
        String actorId,
        RoutingId nodeRid) {
        return renew(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorId(), row.actorType(), row.actorRef(), nodeRid, ZLinkSpotKind.ENTRY,
            "", null, row.ownerId(), row.generation(), row.updatedAt()));
    }

    CompletionStage<Void> release(String actorType, String actorId) {
        ZLinkActorLocationKey key = new ZLinkActorLocationKey(actorId);
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
        TrackedActor tracked;
        synchronized (gate) {
            tracked = actors.remove(canonical);
        }
        if (tracked == null) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.removeActor(key, tracked.row().generation()).thenApply(ignored -> null);
    }

    void abandon(String actorId) {
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actorId));
        synchronized (gate) {
            actors.remove(canonical);
        }
    }

    boolean owns(String actorType, String actorId) {
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actorId));
        synchronized (gate) {
            return actors.containsKey(canonical);
        }
    }

    void onOwnershipLost(String canonicalKey) {
        Runnable deactivate = null;
        synchronized (gate) {
            TrackedActor actor = actors.remove(canonicalKey);
            if (actor != null) {
                deactivate = actor.deactivate();
            }
        }
        if (deactivate != null) {
            deactivate.run();
        }
    }

    private CompletionStage<ZLinkLocationWriteStatus> write(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate,
        ZLinkLocationWriteIntent intent) {
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actorId));
        synchronized (gate) {
            if (actors.containsKey(canonical)) {
                return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
            }
        }

        ZLinkActorLocation row = new ZLinkActorLocation(
            actorId, actorType, null, nodeRid, ZLinkSpotKind.ENTRY, "", null, "", 0, Instant.EPOCH);
        return runtime.writeActor(row, intent)
            .thenApply(result -> {
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    synchronized (gate) {
                        actors.put(canonical, new TrackedActor(
                            rowWithGeneration(row, result.generation()), deactivate));
                    }
                }
                return result.status();
            });
    }

    private CompletionStage<Void> renew(
        String actorType,
        String actorId,
        Function<ZLinkActorLocation, ZLinkActorLocation> mutate) {
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actorId));
        TrackedActor tracked;
        synchronized (gate) {
            tracked = actors.get(canonical);
            if (tracked == null) {
                return CompletableFuture.completedFuture(null);
            }
            tracked = tracked.withRow(mutate.apply(tracked.row()));
            actors.put(canonical, tracked);
        }
        return runtime.writeActor(tracked.row(), ZLinkLocationWriteIntent.RENEW).thenApply(result -> {
            if (result.status() == ZLinkLocationWriteStatus.IGNORED_STALE) {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
                    "Actor '" + actorId + "' location is no longer owned by this runtime.");
            }
            return null;
        });
    }

    private static ZLinkActorLocation rowWithGeneration(ZLinkActorLocation row, long generation) {
        return new ZLinkActorLocation(
            row.actorId(), row.actorType(), row.actorRef(), row.nodeRid(), row.locationKind(),
            row.spotMeshName(), row.spotRid(), row.ownerId(), generation, row.updatedAt());
    }

    private record TrackedActor(ZLinkActorLocation row, Runnable deactivate) {
        TrackedActor withRow(ZLinkActorLocation row) {
            return new TrackedActor(row, deactivate);
        }
    }
}
