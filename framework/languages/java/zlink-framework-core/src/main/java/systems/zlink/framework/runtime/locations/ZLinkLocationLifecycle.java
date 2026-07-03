package systems.zlink.framework.runtime.locations;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class ZLinkLocationLifecycle implements AutoCloseable {
    private final ZLinkLocationRuntime runtime;
    private final Object gate = new Object();
    private final Map<String, TrackedActor> actors = new HashMap<>();
    private final Map<String, TrackedSpot> spots = new HashMap<>();
    private final Map<String, Long> routes = new HashMap<>();
    private final java.util.function.BiConsumer<ZLinkLocationKind, String> ownershipLostListener = this::onOwnershipLost;

    public ZLinkLocationLifecycle(ZLinkLocationRuntime runtime) {
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.runtime.addOwnershipLostListener(ownershipLostListener);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimSpotAsync(
        String meshName,
        RoutingId spotRid,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        Runnable deactivate) {
        ZLinkSpotLocation row = new ZLinkSpotLocation(
            meshName, spotRid, spotType, nodeRid, spotKind, routeEndpoint, "", 0, Instant.EPOCH);
        return runtime.writeSpotAsync(row, ZLinkLocationWriteIntent.NEW_CLAIM)
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

    public CompletionStage<Void> releaseSpotAsync(String meshName, RoutingId spotRid) {
        ZLinkSpotLocationKey key = new ZLinkSpotLocationKey(meshName, spotRid);
        String canonical = ZLinkLocationKeyCodec.encodeSpotKey(key);
        TrackedSpot tracked;
        synchronized (gate) {
            tracked = spots.remove(canonical);
        }
        if (tracked == null) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.removeSpotAsync(key, tracked.generation()).thenApply(ignored -> null);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimActorAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return writeActorAsync(
            actorType,
            actorId,
            nodeRid,
            deactivate,
            ZLinkLocationWriteIntent.NEW_CLAIM);
    }

    public CompletionStage<ZLinkLocationWriteStatus> takeoverActorAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return writeActorAsync(
            actorType,
            actorId,
            nodeRid,
            deactivate,
            ZLinkLocationWriteIntent.TAKEOVER);
    }

    private CompletionStage<ZLinkLocationWriteStatus> writeActorAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate,
        ZLinkLocationWriteIntent intent) {
        String normalizedType = ZLinkLocationKeyCodec.normalizeActorType(actorType);
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(normalizedType, actorId));
        synchronized (gate) {
            if (actors.containsKey(canonical)) {
                return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
            }
        }

        ZLinkActorLocation row = new ZLinkActorLocation(
            normalizedType, actorId, "", nodeRid, 0, ZLinkSpotKind.ENTRY, null, null,
            ZLinkSpotKind.ENTRY, "", Instant.EPOCH);
        return runtime.writeActorAsync(row, intent)
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

    public CompletionStage<Void> setActorRefAsync(String actorType, String actorId, String actorRef) {
        return renewActorAsync(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorType(), row.actorId(), actorRef, row.nodeRid(), row.generation(),
            row.locationKind(), row.spotMeshName(), row.spotRid(), row.spotKind(),
            row.ownerId(), row.updatedAt()));
    }

    public CompletionStage<Void> notifyActorJoinedSpotAsync(
        String actorType,
        String actorId,
        String meshName,
        RoutingId spotRid) {
        return renewActorAsync(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorType(), row.actorId(), row.actorRef(), row.nodeRid(), row.generation(),
            ZLinkSpotKind.USER, meshName, spotRid, ZLinkSpotKind.USER, row.ownerId(), row.updatedAt()));
    }

    public CompletionStage<Void> notifyActorLeftSpotAsync(String actorType, String actorId) {
        return renewActorAsync(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorType(), row.actorId(), row.actorRef(), row.nodeRid(), row.generation(),
            ZLinkSpotKind.ENTRY, null, null, ZLinkSpotKind.ENTRY, row.ownerId(), row.updatedAt()));
    }

    public CompletionStage<Void> notifyActorMovedToEntrySpotAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid) {
        return renewActorAsync(actorType, actorId, row -> new ZLinkActorLocation(
            row.actorType(), row.actorId(), row.actorRef(), nodeRid, row.generation(),
            ZLinkSpotKind.ENTRY, null, null, ZLinkSpotKind.ENTRY, row.ownerId(), row.updatedAt()));
    }

    public CompletionStage<Void> releaseActorAsync(String actorType, String actorId) {
        ZLinkActorLocationKey key = new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.normalizeActorType(actorType), actorId);
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(key);
        TrackedActor tracked;
        synchronized (gate) {
            tracked = actors.remove(canonical);
        }
        if (tracked == null) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.removeActorAsync(key, tracked.row().generation()).thenApply(ignored -> null);
    }

    public CompletionStage<Void> bindActorSessionRouteAsync(
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
        return runtime.writeRouteAsync(row, ZLinkLocationWriteIntent.NEW_CLAIM)
            .thenCompose(result -> {
                if (result.status() == ZLinkLocationWriteStatus.REJECTED_CONFLICT) {
                    return runtime.writeRouteAsync(row, ZLinkLocationWriteIntent.TAKEOVER);
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

    public CompletionStage<Void> removeActorSessionRouteAsync(RoutingId sessionRid) {
        ZLinkRouteLocationKey key = new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, toRouteKey(sessionRid));
        String canonical = ZLinkLocationKeyCodec.encodeRouteKey(key);
        Long generation;
        synchronized (gate) {
            generation = routes.remove(canonical);
        }
        if (generation == null) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.removeRouteAsync(key, generation).thenApply(ignored -> null);
    }

    boolean ownsActor(String actorType, String actorId) {
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.normalizeActorType(actorType), actorId));
        synchronized (gate) {
            return actors.containsKey(canonical);
        }
    }

    @Override
    public void close() {
        runtime.removeOwnershipLostListener(ownershipLostListener);
    }

    private CompletionStage<Void> renewActorAsync(
        String actorType,
        String actorId,
        Function<ZLinkActorLocation, ZLinkActorLocation> mutate) {
        String canonical = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.normalizeActorType(actorType), actorId));
        TrackedActor tracked;
        synchronized (gate) {
            tracked = actors.get(canonical);
            if (tracked == null) {
                return CompletableFuture.completedFuture(null);
            }
            tracked = tracked.withRow(mutate.apply(tracked.row()));
            actors.put(canonical, tracked);
        }
        return runtime.writeActorAsync(tracked.row(), ZLinkLocationWriteIntent.RENEW).thenApply(ignored -> null);
    }

    private void onOwnershipLost(ZLinkLocationKind kind, String canonicalKey) {
        Runnable deactivate = null;
        synchronized (gate) {
            if (kind == ZLinkLocationKind.ACTOR) {
                TrackedActor actor = actors.remove(canonicalKey);
                deactivate = actor == null ? null : actor.deactivate();
            } else if (kind == ZLinkLocationKind.SPOT) {
                TrackedSpot spot = spots.remove(canonicalKey);
                deactivate = spot == null ? null : spot.deactivate();
            } else if (kind == ZLinkLocationKind.ROUTE) {
                routes.remove(canonicalKey);
            }
        }
        if (deactivate != null) {
            deactivate.run();
        }
    }

    private static ZLinkActorLocation rowWithGeneration(ZLinkActorLocation row, long generation) {
        return new ZLinkActorLocation(
            row.actorType(), row.actorId(), row.actorRef(), row.nodeRid(), generation,
            row.locationKind(), row.spotMeshName(), row.spotRid(), row.spotKind(),
            row.ownerId(), row.updatedAt());
    }

    private static String toRouteKey(RoutingId routingId) {
        return java.util.HexFormat.of().formatHex(routingId.toBytes());
    }

    public static String serializeActorRef(RoutingId nodeRid, String actorId, long generation) {
        return java.util.HexFormat.of().formatHex(nodeRid.toBytes())
            + ":"
            + Long.toUnsignedString(generation)
            + ":"
            + actorId;
    }

    private record TrackedActor(ZLinkActorLocation row, Runnable deactivate) {
        TrackedActor withRow(ZLinkActorLocation row) {
            return new TrackedActor(row, deactivate);
        }
    }

    private record TrackedSpot(long generation, Runnable deactivate) {
    }
}
