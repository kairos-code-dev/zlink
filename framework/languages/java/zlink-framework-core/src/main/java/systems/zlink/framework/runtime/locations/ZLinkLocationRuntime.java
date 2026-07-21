package systems.zlink.framework.runtime.locations;

import java.time.Duration;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

public final class ZLinkLocationRuntime implements AutoCloseable {
    private final ZLinkRegisteredLocationStores stores;
    private final String ownerId;
    private final Duration ownerLeaseTtl;
    private final Duration heartbeatInterval;
    private final CopyOnWriteArrayList<BiConsumer<ZLinkLocationKind, String>> ownershipLostListeners = new CopyOnWriteArrayList<>();
    private final ScheduledExecutorService heartbeatExecutor;
    private final Object stateGate = new Object();
    private final java.util.concurrent.atomic.AtomicBoolean heartbeatInFlight =
        new java.util.concurrent.atomic.AtomicBoolean();
    private ScheduledFuture<?> heartbeatTask;
    private RoutingId nodeRid;
    private boolean started;
    private volatile boolean ownerLeaseHealthy;
    private volatile String lastError;
    private volatile java.time.Instant ownerLeaseRenewedAt;
    private volatile long nextOwnerLeaseRenewalNanos;

    ZLinkLocationRuntime(
        ZLinkLocationStore store,
        Duration ownerLeaseTtl,
        Duration heartbeatInterval) {
        this(ZLinkRegisteredLocationStores.fromUnified(store), UUID.randomUUID().toString().replace("-", ""), ownerLeaseTtl, heartbeatInterval);
    }

    ZLinkLocationRuntime(
        ZLinkLocationStore store,
        String ownerId,
        Duration ownerLeaseTtl,
        Duration heartbeatInterval) {
        this(ZLinkRegisteredLocationStores.fromUnified(store), ownerId, ownerLeaseTtl, heartbeatInterval);
    }

    public ZLinkLocationRuntime(
        ZLinkRegisteredLocationStores stores,
        Duration ownerLeaseTtl,
        Duration heartbeatInterval) {
        this(stores, UUID.randomUUID().toString().replace("-", ""), ownerLeaseTtl, heartbeatInterval);
    }

    ZLinkLocationRuntime(
        ZLinkRegisteredLocationStores stores,
        String ownerId,
        Duration ownerLeaseTtl,
        Duration heartbeatInterval) {
        this.stores = Objects.requireNonNull(stores, "stores");
        this.ownerId = requireText(ownerId, "ownerId");
        this.ownerLeaseTtl = requirePositive(ownerLeaseTtl, "ownerLeaseTtl");
        this.heartbeatInterval = requirePositive(heartbeatInterval, "heartbeatInterval");
        this.heartbeatExecutor = Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-location-owner-lease");
            thread.setDaemon(true);
            return thread;
        });
    }

    public String ownerId() {
        return ownerId;
    }

    public boolean ownerLeaseHealthy() {
        return ownerLeaseHealthy;
    }

    public String lastError() {
        return lastError;
    }

    public java.time.Instant ownerLeaseRenewedAt() {
        return ownerLeaseRenewedAt;
    }

    public CompletionStage<Void> start(RoutingId nodeRid) {
        Objects.requireNonNull(nodeRid, "nodeRid");
        synchronized (stateGate) {
            if (started) {
                return CompletableFuture.completedFuture(null);
            }
            started = true;
            this.nodeRid = nodeRid;
        }

        return renewOwnerLeaseOnce().thenAccept(ignored -> {
            synchronized (stateGate) {
                if (heartbeatTask == null || heartbeatTask.isCancelled()) {
                    heartbeatTask = heartbeatExecutor.scheduleWithFixedDelay(
                        this::renewOwnerLeaseOnHeartbeat,
                        heartbeatInterval.toMillis(),
                        heartbeatInterval.toMillis(),
                        TimeUnit.MILLISECONDS);
                }
            }
        });
    }

    public CompletionStage<Void> stop() {
        boolean shouldStop;
        synchronized (stateGate) {
            shouldStop = started;
            started = false;
            if (heartbeatTask != null) {
                heartbeatTask.cancel(false);
                heartbeatTask = null;
            }
        }
        if (!shouldStop) {
            return CompletableFuture.completedFuture(null);
        }

        return stores.unifiedStore().removeAllByOwner(ownerId)
            .thenCompose(ignored -> stores.ownerLeaseStore().removeOwnerLease(ownerId))
            .thenApply(ignored -> null);
    }

    public CompletionStage<Boolean> renewOwnerLeaseOnce() {
        RoutingId currentNodeRid = nodeRid;
        if (currentNodeRid == null) {
            CompletableFuture<Boolean> failed = new CompletableFuture<>();
            failed.completeExceptionally(new IllegalStateException("Location runtime must be started before renewing its owner lease."));
            return failed;
        }

        return stores.ownerLeaseStore().renewOwnerLease(ownerId, currentNodeRid, ownerLeaseTtl)
            .handle((result, failure) -> {
                if (failure != null) {
                    recordFailure(failure.getMessage());
                    return false;
                }
                recordSuccessfulRenewal(result.storeNow());
                nextOwnerLeaseRenewalNanos = System.nanoTime() + heartbeatInterval.toNanos();
                return true;
            });
    }

    CompletionStage<ZLinkLocationWriteResult> writePeer(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        ZLinkPeerLocation stamped = new ZLinkPeerLocation(
            peer.autoConnectType(), peer.meshName(), peer.nodeRid(), peer.role(), peer.endpoint(),
            peer.weight(), peer.draining(), peer.value(), peer.metadata(), peer.capabilities(), ownerId,
            peer.generation(), peer.updatedAt());
        String key = ZLinkLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
            peer.autoConnectType(), peer.meshName(), peer.role(), peer.nodeRid(), peer.endpoint()));
        return stores.peerStore().updatePeer(stamped, intent)
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.PEER, key));
    }

    CompletionStage<ZLinkLocationWriteResult> writeSpot(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        ZLinkSpotLocation stamped = new ZLinkSpotLocation(
            spot.meshName(), spot.spotRid(), spot.spotGeneration(), spot.spotType(),
            spot.nodeRid(), spot.spotKind(),
            spot.routeEndpoint(), ownerId, spot.generation(), spot.updatedAt());
        String key = ZLinkLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(spot.meshName(), spot.spotRid()));
        return stores.spotStore().updateSpot(stamped, intent)
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.SPOT, key));
    }

    CompletionStage<ZLinkLocationWriteResult> writeActor(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        ZLinkActorLocation stamped = new ZLinkActorLocation(
            actor.actorId(), actor.actorType(), actor.actorRef(), actor.nodeRid(), actor.locationKind(),
            actor.spotMeshName(), actor.spotRid(), ownerId, actor.generation(), actor.updatedAt());
        String key = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actor.actorId()));
        return stores.actorStore().updateActor(stamped, intent)
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ACTOR, key));
    }

    CompletionStage<ZLinkLocationWriteResult> writeRoute(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        ZLinkRouteLocation stamped = new ZLinkRouteLocation(
            route.routeKind(), route.routeKey(), route.ownerNodeRid(), ownerId,
            route.generation(), route.value(), route.updatedAt());
        String key = ZLinkLocationKeyCodec.encodeRouteKey(new ZLinkRouteLocationKey(route.routeKind(), route.routeKey()));
        return stores.routeStore().updateRoute(stamped, intent)
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ROUTE, key));
    }

    CompletionStage<ZLinkLocationWriteResult> removeSpot(
        ZLinkSpotLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodeSpotKey(key);
        return stores.spotStore().removeSpot(key, new ZLinkLocationOwnerToken(ownerId, generation))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.SPOT, canonicalKey));
    }

    CompletionStage<ZLinkLocationWriteResult> removePeer(
        ZLinkPeerLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodePeerKey(key);
        return stores.peerStore().removePeer(key, new ZLinkLocationOwnerToken(ownerId, generation))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.PEER, canonicalKey));
    }

    CompletionStage<ZLinkLocationWriteResult> removeActor(
        ZLinkActorLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodeActorKey(key);
        return stores.actorStore().removeActor(key, new ZLinkLocationOwnerToken(ownerId, generation))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ACTOR, canonicalKey));
    }

    CompletionStage<ZLinkLocationWriteResult> removeRoute(
        ZLinkRouteLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodeRouteKey(key);
        return stores.routeStore().removeRoute(key, new ZLinkLocationOwnerToken(ownerId, generation))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ROUTE, canonicalKey));
    }

    void addOwnershipLostListener(BiConsumer<ZLinkLocationKind, String> listener) {
        ownershipLostListeners.add(Objects.requireNonNull(listener, "listener"));
    }

    void removeOwnershipLostListener(BiConsumer<ZLinkLocationKind, String> listener) {
        ownershipLostListeners.remove(listener);
    }

    @Override
    public void close() {
        synchronized (stateGate) {
            if (heartbeatTask != null) {
                heartbeatTask.cancel(false);
                heartbeatTask = null;
            }
        }
        heartbeatExecutor.shutdownNow();
    }

    private void renewOwnerLeaseOnHeartbeat() {
        if (!heartbeatInFlight.compareAndSet(false, true)) {
            return;
        }
        long expected = nextOwnerLeaseRenewalNanos;
        if (expected != 0L) {
            long lateNanos = Math.max(0L, System.nanoTime() - expected);
            systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics.record(
                "zlink.location.owner_lease.renew.lateness",
                java.time.Duration.ofNanos(lateNanos),
                java.util.Map.of());
        }
        renewOwnerLeaseOnce().whenComplete((ignored, failure) -> heartbeatInFlight.set(false));
    }

    private void recordSuccessfulRenewal(java.time.Instant storeNow) {
        synchronized (stateGate) {
            java.time.Instant previous = ownerLeaseRenewedAt;
            ownerLeaseRenewedAt = previous == null || storeNow.isAfter(previous)
                ? storeNow
                : previous.plusNanos(1L);
            ownerLeaseHealthy = true;
            lastError = null;
        }
    }

    private ZLinkLocationWriteResult notifyIfStale(
        ZLinkLocationWriteResult result,
        ZLinkLocationKind kind,
        String canonicalKey) {
        if (result.status() == ZLinkLocationWriteStatus.IGNORED_STALE) {
            for (BiConsumer<ZLinkLocationKind, String> listener : ownershipLostListeners) {
                listener.accept(kind, canonicalKey);
            }
        }
        return result;
    }

    private void recordFailure(String message) {
        ownerLeaseHealthy = false;
        lastError = message;
    }

    private static String requireText(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(name + " must not be blank.");
        }
        return value;
    }

    private static Duration requirePositive(Duration value, String name) {
        if (value == null || value.isZero() || value.isNegative()) {
            throw new IllegalArgumentException(name + " must be positive.");
        }
        return value;
    }
}
