package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.LongSupplier;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;

final class ZLinkAutoConnectReconciler {
    private final ZLinkAutoConnectPlanner.Local local;
    private final ZLinkPeerLocation localRow;
    private final ZLinkLocationRuntime runtime;
    private final ZLinkPeerLocationResolver peers;
    private final ZLinkAutoConnectExecutor executor;
    private final ZLinkLocationOptions options;
    private final LongSupplier nanoTime;
    private final Map<String, ZLinkAutoConnectPlanner.Target> active = new HashMap<>();
    private Map<String, ZLinkAutoConnectPlanner.Target> lastDesired = Map.of();
    private final Map<String, ZLinkAutoConnectPlanner.Target> observedManual = new HashMap<>();
    private long localGeneration;
    private boolean localPublished;
    private boolean storeFailed;
    private long storeFailureStartedNanos = -1;
    private boolean draining;
    private long recoveryDeferUntilNanos;

    ZLinkAutoConnectReconciler(
        ZLinkAutoConnectPlanner.Local local,
        ZLinkPeerLocation localRow,
        ZLinkLocationRuntime runtime,
        ZLinkPeerLocationResolver peers,
        ZLinkAutoConnectExecutor executor,
        ZLinkLocationOptions options) {
        this(local, localRow, runtime, peers, executor, options, System::nanoTime);
    }

    ZLinkAutoConnectReconciler(
        ZLinkAutoConnectPlanner.Local local,
        ZLinkPeerLocation localRow,
        ZLinkLocationRuntime runtime,
        ZLinkPeerLocationResolver peers,
        ZLinkAutoConnectExecutor executor,
        ZLinkLocationOptions options,
        LongSupplier nanoTime) {
        this.local = Objects.requireNonNull(local, "local");
        this.localRow = localRow;
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.peers = Objects.requireNonNull(peers, "peers");
        this.executor = Objects.requireNonNull(executor, "executor");
        this.options = Objects.requireNonNull(options, "options");
        this.nanoTime = Objects.requireNonNull(nanoTime, "nanoTime");
    }

    boolean storeFailed() {
        return storeFailed;
    }

    CompletionStage<Void> tick() {
        return publishLocal()
            .thenCompose(ignored -> peers.listLivePeers(new ZLinkPeerLocationFilter(
                local.type(), local.meshName(), null, null, null)))
            .handle((rows, failure) -> {
                if (failure != null) {
                    if (!storeFailed) {
                        storeFailureStartedNanos = nanoTime.getAsLong();
                    }
                    storeFailed = true;
                    localPublished = false;
                    retryPendingTargetsWithinStoreFailureGrace();
                    return null;
                }
                reconcile(rows);
                return null;
            });
    }

    CompletionStage<Void> shutdown() {
        CompletionStage<Void> remove = CompletableFuture.completedFuture(null);
        if (localPublished) {
            remove = runtime.removePeer(localKey(), localGeneration).thenApply(ignored -> null);
            localPublished = false;
        }
        return remove.whenComplete((ignored, failure) -> {
            for (ZLinkAutoConnectPlanner.Target target : active.values()) {
                executor.disconnect(target);
            }
            active.clear();
        });
    }

    CompletionStage<Void> markDraining() {
        draining = true;
        if (localRow == null) {
            return CompletableFuture.completedFuture(null);
        }
        return publishLocal().thenCompose(ignored -> {
            if (!localPublished) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException("local peer row is not published"));
            }
            return runtime.writePeer(withCurrentState(localGeneration), ZLinkLocationWriteIntent.RENEW)
                .thenCompose(result -> result.status() == ZLinkLocationWriteStatus.STORED
                    ? CompletableFuture.completedFuture(null)
                    : CompletableFuture.failedFuture(
                        new IllegalStateException("failed to publish draining peer row")));
        });
    }

    private void reconcile(List<ZLinkPeerLocation> rows) {
        if (storeFailed) {
            storeFailed = false;
            storeFailureStartedNanos = -1;
            recoveryDeferUntilNanos = nanoTime.getAsLong()
                + Math.max(
                    options.heartbeatInterval().toNanos(),
                    options.ownerLeaseTtl().toNanos());
        }

        Map<String, ZLinkAutoConnectPlanner.Target> desired =
            ZLinkAutoConnectPlanner.computeDesired(local, rows);
        lastDesired = Map.copyOf(desired);
        Map<String, ZLinkAutoConnectPlanner.Target> manualSnapshot = new HashMap<>();
        for (ZLinkPeerLocation row : rows) {
            ZLinkAutoConnectPlanner.Target target =
                ZLinkAutoConnectPlanner.trackableTarget(local, row);
            if (target == null || !executor.isManual(target) || desired.containsKey(target.key())) {
                continue;
            }
            manualSnapshot.put(target.key(), target);
            ZLinkAutoConnectPlanner.Target previous = observedManual.get(target.key());
            if (previous != null
                && (!previous.endpoint().equals(target.endpoint())
                    || !Objects.equals(previous.ownerId(), target.ownerId()))) {
                executor.replace(previous, target);
            }
        }
        observedManual.putAll(manualSnapshot);
        List<String> toRemove = new ArrayList<>();
        for (Map.Entry<String, ZLinkAutoConnectPlanner.Target> entry : desired.entrySet()) {
            ZLinkAutoConnectPlanner.Target current = active.get(entry.getKey());
            ZLinkAutoConnectPlanner.Target target = entry.getValue();
            if (current == null) {
                if (executor.connect(target)) {
                    active.put(entry.getKey(), target);
                }
                continue;
            }
            if (!current.endpoint().equals(target.endpoint())
                || !Objects.equals(current.ownerId(), target.ownerId())) {
                if (executor.replace(current, target)) {
                    active.put(entry.getKey(), target);
                }
            }
        }
        if (nanoTime.getAsLong() >= recoveryDeferUntilNanos) {
            for (String key : active.keySet()) {
                if (!desired.containsKey(key)) {
                    toRemove.add(key);
                }
            }
            for (String key : toRemove) {
                ZLinkAutoConnectPlanner.Target target = active.get(key);
                if (executor.disconnect(target)) {
                    active.remove(key);
                }
            }
        }
    }

    private void retryPendingTargetsWithinStoreFailureGrace() {
        if (storeFailureStartedNanos < 0
            || options.storeFailureGrace().isZero()
            || options.storeFailureGrace().isNegative()
            || nanoTime.getAsLong() - storeFailureStartedNanos
                > options.storeFailureGrace().toNanos()) {
            return;
        }
        for (Map.Entry<String, ZLinkAutoConnectPlanner.Target> entry : lastDesired.entrySet()) {
            if (!active.containsKey(entry.getKey()) && executor.connect(entry.getValue())) {
                active.put(entry.getKey(), entry.getValue());
            }
        }
    }

    private CompletionStage<Void> publishLocal() {
        if (localRow == null || localPublished) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.writePeer(withCurrentState(localGeneration), ZLinkLocationWriteIntent.NEW_CLAIM)
            .thenCompose(result -> {
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    localGeneration = result.generation();
                    localPublished = true;
                    return CompletableFuture.<Void>completedFuture(null);
                }
                if (result.status() == ZLinkLocationWriteStatus.REJECTED_CONFLICT
                    && localGeneration > 0) {
                    ZLinkPeerLocation renewed = withCurrentState(localGeneration);
                    return runtime.writePeer(renewed, ZLinkLocationWriteIntent.RENEW)
                        .thenAccept(renewedResult -> {
                            localPublished = renewedResult.status() == ZLinkLocationWriteStatus.STORED;
                        });
                }
                return CompletableFuture.<Void>completedFuture(null);
            });
    }

    private ZLinkPeerLocation withCurrentState(long generation) {
        return new ZLinkPeerLocation(
            localRow.autoConnectType(), localRow.meshName(), localRow.nodeRid(),
            localRow.role(), localRow.endpoint(), localRow.weight(), draining, localRow.value(),
            localRow.metadata(), localRow.capabilities(), localRow.ownerId(), generation, Instant.EPOCH);
    }

    private ZLinkPeerLocationKey localKey() {
        return new ZLinkPeerLocationKey(
            localRow.autoConnectType(),
            localRow.meshName(),
            localRow.role(),
            localRow.nodeRid(),
            localRow.endpoint());
    }

}
