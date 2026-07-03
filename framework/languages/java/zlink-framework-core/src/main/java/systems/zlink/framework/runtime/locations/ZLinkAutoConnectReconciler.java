package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
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
    private final Map<String, ZLinkAutoConnectPlanner.Target> active = new HashMap<>();
    private long localGeneration;
    private boolean localPublished;
    private boolean storeFailed;
    private long recoveryDeferUntilNanos;

    ZLinkAutoConnectReconciler(
        ZLinkAutoConnectPlanner.Local local,
        ZLinkPeerLocation localRow,
        ZLinkLocationRuntime runtime,
        ZLinkPeerLocationResolver peers,
        ZLinkAutoConnectExecutor executor,
        ZLinkLocationOptions options) {
        this.local = Objects.requireNonNull(local, "local");
        this.localRow = localRow;
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.peers = Objects.requireNonNull(peers, "peers");
        this.executor = Objects.requireNonNull(executor, "executor");
        this.options = Objects.requireNonNull(options, "options");
    }

    boolean storeFailed() {
        return storeFailed;
    }

    CompletionStage<Void> tickAsync() {
        return publishLocalAsync()
            .thenCompose(ignored -> peers.listPeersAsync(new ZLinkPeerLocationFilter(
                local.type(), local.meshName(), null, null, null)))
            .handle((rows, failure) -> {
                if (failure != null) {
                    storeFailed = true;
                    localPublished = false;
                    return null;
                }
                reconcile(rows);
                return null;
            });
    }

    CompletionStage<Void> shutdownAsync() {
        CompletionStage<Void> remove = CompletableFuture.completedFuture(null);
        if (localPublished) {
            remove = runtime.removePeerAsync(localKey(), localGeneration).thenApply(ignored -> null);
            localPublished = false;
        }
        return remove.whenComplete((ignored, failure) -> {
            for (ZLinkAutoConnectPlanner.Target target : active.values()) {
                executor.disconnect(target);
            }
            active.clear();
        });
    }

    private void reconcile(List<ZLinkPeerLocation> rows) {
        if (storeFailed) {
            storeFailed = false;
            recoveryDeferUntilNanos = System.nanoTime() + options.heartbeatInterval().toNanos();
        }

        Map<String, ZLinkAutoConnectPlanner.Target> desired =
            ZLinkAutoConnectPlanner.computeDesired(local, rows);
        List<String> toRemove = new ArrayList<>();
        for (Map.Entry<String, ZLinkAutoConnectPlanner.Target> entry : desired.entrySet()) {
            ZLinkAutoConnectPlanner.Target current = active.get(entry.getKey());
            ZLinkAutoConnectPlanner.Target target = entry.getValue();
            if (current == null) {
                executor.connect(target);
                active.put(entry.getKey(), target);
                continue;
            }
            if (!current.endpoint().equals(target.endpoint())
                || !Objects.equals(current.ownerId(), target.ownerId())) {
                executor.disconnect(current);
                executor.connect(target);
                active.put(entry.getKey(), target);
            }
        }
        if (System.nanoTime() >= recoveryDeferUntilNanos) {
            for (String key : active.keySet()) {
                if (!desired.containsKey(key)) {
                    toRemove.add(key);
                }
            }
            for (String key : toRemove) {
                executor.disconnect(active.remove(key));
            }
        }
    }

    private CompletionStage<Void> publishLocalAsync() {
        if (localRow == null || localPublished) {
            return CompletableFuture.completedFuture(null);
        }
        return runtime.writePeerAsync(localRow, ZLinkLocationWriteIntent.NEW_CLAIM)
            .thenCompose(result -> {
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    localGeneration = result.generation();
                    localPublished = true;
                    return CompletableFuture.completedFuture(null);
                }
                if (result.status() == ZLinkLocationWriteStatus.REJECTED_CONFLICT
                    && localGeneration > 0) {
                    ZLinkPeerLocation renewed = new ZLinkPeerLocation(
                        localRow.autoConnectType(), localRow.meshName(), localRow.nodeRid(),
                        localRow.role(), localRow.endpoint(), localRow.weight(), localRow.value(),
                        localRow.metadata(), localRow.capabilities(), localRow.ownerId(),
                        localGeneration, Instant.EPOCH);
                    return runtime.writePeerAsync(renewed, ZLinkLocationWriteIntent.RENEW)
                        .thenApply(renewedResult -> {
                            localPublished = renewedResult.status() == ZLinkLocationWriteStatus.STORED;
                            return null;
                        });
                }
                return CompletableFuture.completedFuture(null);
            });
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
