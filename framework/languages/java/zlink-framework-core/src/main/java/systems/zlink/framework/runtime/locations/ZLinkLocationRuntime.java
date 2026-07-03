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
import java.util.function.Supplier;
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
    private ScheduledFuture<?> heartbeatTask;
    private RoutingId nodeRid;
    private boolean started;
    private volatile boolean ownerLeaseHealthy;
    private volatile String lastError;
    private volatile java.time.Instant ownerLeaseRenewedAt;

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

    public CompletionStage<Void> startAsync(RoutingId nodeRid) {
        Objects.requireNonNull(nodeRid, "nodeRid");
        synchronized (stateGate) {
            if (started) {
                return CompletableFuture.completedFuture(null);
            }
            started = true;
            this.nodeRid = nodeRid;
        }

        return renewOwnerLeaseOnceAsync().thenAccept(ignored -> {
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

    public CompletionStage<Void> stopAsync() {
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

        return stores.ownerLeaseStore().removeOwnerLeaseAsync(ownerId)
            .handle((ignored, failure) -> null)
            .thenCompose(ignored -> stores.peerStore().removePeersByOwnerAsync(ownerId).handle((removed, failure) -> null))
            .thenCompose(ignored -> stores.spotStore().removeSpotsByOwnerAsync(ownerId).handle((removed, failure) -> null))
            .thenCompose(ignored -> stores.actorStore().removeActorsByOwnerAsync(ownerId).handle((removed, failure) -> null))
            .thenCompose(ignored -> stores.routeStore().removeRoutesByOwnerAsync(ownerId).handle((removed, failure) -> null))
            .thenApply(ignored -> null);
    }

    public CompletionStage<Boolean> renewOwnerLeaseOnceAsync() {
        RoutingId currentNodeRid = nodeRid;
        if (currentNodeRid == null) {
            CompletableFuture<Boolean> failed = new CompletableFuture<>();
            failed.completeExceptionally(new IllegalStateException("Location runtime must be started before renewing its owner lease."));
            return failed;
        }

        return stores.ownerLeaseStore().renewOwnerLeaseAsync(ownerId, currentNodeRid, ownerLeaseTtl)
            .handle((result, failure) -> {
                if (failure != null) {
                    recordFailure(failure.getMessage());
                    return false;
                }
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    ownerLeaseHealthy = true;
                    ownerLeaseRenewedAt = result.updatedAt();
                    lastError = null;
                    return true;
                }
                recordFailure("Owner lease renew returned " + result.status() + ".");
                return false;
            });
    }

    CompletionStage<ZLinkLocationWriteResult> writePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        ZLinkPeerLocation stamped = new ZLinkPeerLocation(
            peer.autoConnectType(), peer.meshName(), peer.nodeRid(), peer.role(), peer.endpoint(),
            peer.weight(), peer.value(), peer.metadata(), peer.capabilities(), ownerId,
            peer.generation(), peer.updatedAt());
        String key = ZLinkLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
            peer.autoConnectType(), peer.meshName(), peer.role(), peer.nodeRid(), peer.endpoint()));
        return guardStoreWrite(() -> stores.peerStore().updatePeerAsync(stamped, intent))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.PEER, key));
    }

    CompletionStage<ZLinkLocationWriteResult> writeSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        ZLinkSpotLocation stamped = new ZLinkSpotLocation(
            spot.meshName(), spot.spotRid(), spot.spotType(), spot.nodeRid(), spot.spotKind(),
            spot.routeEndpoint(), ownerId, spot.generation(), spot.updatedAt());
        String key = ZLinkLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(spot.meshName(), spot.spotRid()));
        return guardStoreWrite(() -> stores.spotStore().updateSpotAsync(stamped, intent))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.SPOT, key));
    }

    CompletionStage<ZLinkLocationWriteResult> writeActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        String actorType = ZLinkLocationKeyCodec.normalizeActorType(actor.actorType());
        ZLinkActorLocation stamped = new ZLinkActorLocation(
            actorType, actor.actorId(), actor.actorRef(), actor.nodeRid(), actor.generation(),
            actor.locationKind(), actor.spotMeshName(), actor.spotRid(), actor.spotKind(),
            ownerId, actor.updatedAt());
        String key = ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actorType, actor.actorId()));
        return guardStoreWrite(() -> stores.actorStore().updateActorAsync(stamped, intent))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ACTOR, key));
    }

    CompletionStage<ZLinkLocationWriteResult> writeRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        ZLinkRouteLocation stamped = new ZLinkRouteLocation(
            route.routeKind(), route.routeKey(), route.ownerNodeRid(), ownerId,
            route.generation(), route.value(), route.updatedAt());
        String key = ZLinkLocationKeyCodec.encodeRouteKey(new ZLinkRouteLocationKey(route.routeKind(), route.routeKey()));
        return guardStoreWrite(() -> stores.routeStore().updateRouteAsync(stamped, intent))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ROUTE, key));
    }

    CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
        ZLinkSpotLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodeSpotKey(key);
        return guardStoreWrite(() -> stores.spotStore().removeSpotAsync(key, new ZLinkLocationOwnerToken(ownerId, generation)))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.SPOT, canonicalKey));
    }

    CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
        ZLinkPeerLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodePeerKey(key);
        return guardStoreWrite(() -> stores.peerStore().removePeerAsync(key, new ZLinkLocationOwnerToken(ownerId, generation)))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.PEER, canonicalKey));
    }

    CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        long generation) {
        ZLinkActorLocationKey normalized = new ZLinkActorLocationKey(
            ZLinkLocationKeyCodec.normalizeActorType(key.actorType()), key.actorId());
        String canonicalKey = ZLinkLocationKeyCodec.encodeActorKey(normalized);
        return guardStoreWrite(() -> stores.actorStore().removeActorAsync(normalized, new ZLinkLocationOwnerToken(ownerId, generation)))
            .thenApply(result -> notifyIfStale(result, ZLinkLocationKind.ACTOR, canonicalKey));
    }

    CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
        ZLinkRouteLocationKey key,
        long generation) {
        String canonicalKey = ZLinkLocationKeyCodec.encodeRouteKey(key);
        return guardStoreWrite(() -> stores.routeStore().removeRouteAsync(key, new ZLinkLocationOwnerToken(ownerId, generation)))
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
        renewOwnerLeaseOnceAsync().toCompletableFuture().join();
    }

    private CompletionStage<ZLinkLocationWriteResult> guardStoreWrite(Supplier<CompletionStage<ZLinkLocationWriteResult>> write) {
        try {
            return write.get().exceptionally(failure -> {
                recordFailure(failure.getMessage());
                return ZLinkLocationWriteResult.storeUnavailable();
            });
        } catch (RuntimeException failure) {
            recordFailure(failure.getMessage());
            return CompletableFuture.completedFuture(ZLinkLocationWriteResult.storeUnavailable());
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
