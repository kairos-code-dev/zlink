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
            .thenCompose(ignored -> peers.listLivePeersAsync(new ZLinkPeerLocationFilter(
                local.type(), local.meshName(), null, null, null)))
            .handle((rows, failure) -> {
                if (failure != null) {
                    boot("tick fail local=" + localSummary() + " error=" + failure.getClass().getSimpleName());
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

        boot("tick peers count=" + rows.size() + " local=" + localSummary());
        for (ZLinkAutoConnectPlanner.PeerDecision decision : ZLinkAutoConnectPlanner.decideAll(local, rows)) {
            boot("tick peer " + peerSummary(decision.peer()) + " decision=" + decisionSummary(decision));
        }
        Map<String, ZLinkAutoConnectPlanner.Target> desired =
            ZLinkAutoConnectPlanner.computeDesired(local, rows);
        boot("tick desired count=" + desired.size() + " active=" + active.size() + " local=" + localSummary());
        List<String> toRemove = new ArrayList<>();
        for (Map.Entry<String, ZLinkAutoConnectPlanner.Target> entry : desired.entrySet()) {
            ZLinkAutoConnectPlanner.Target current = active.get(entry.getKey());
            ZLinkAutoConnectPlanner.Target target = entry.getValue();
            if (current == null) {
                boot("connect start local=" + localSummary() + " target=" + targetSummary(target));
                executor.connect(target);
                boot("connect done local=" + localSummary() + " target=" + targetSummary(target));
                active.put(entry.getKey(), target);
                continue;
            }
            if (!current.endpoint().equals(target.endpoint())
                || !Objects.equals(current.ownerId(), target.ownerId())) {
                boot("reconnect disconnect local=" + localSummary() + " target=" + targetSummary(current));
                executor.disconnect(current);
                boot("reconnect connect local=" + localSummary() + " target=" + targetSummary(target));
                executor.connect(target);
                boot("reconnect done local=" + localSummary() + " target=" + targetSummary(target));
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
                ZLinkAutoConnectPlanner.Target target = active.remove(key);
                boot("disconnect start local=" + localSummary() + " target=" + targetSummary(target));
                executor.disconnect(target);
                boot("disconnect done local=" + localSummary() + " target=" + targetSummary(target));
            }
        }
    }

    private CompletionStage<Void> publishLocalAsync() {
        if (localRow == null || localPublished) {
            return CompletableFuture.completedFuture(null);
        }
        String key = ZLinkLocationKeyCodec.encodePeerKey(localKey());
        boot("claim attempt key=" + key + " local=" + localSummary());
        return runtime.writePeerAsync(localRow, ZLinkLocationWriteIntent.NEW_CLAIM)
            .thenCompose(result -> {
                boot(
                    "claim result key=" + key
                        + " status=" + result.status()
                        + " generation=" + result.generation()
                        + " local=" + localSummary());
                if (result.status() == ZLinkLocationWriteStatus.STORED) {
                    localGeneration = result.generation();
                    localPublished = true;
                    return CompletableFuture.<Void>completedFuture(null);
                }
                if (result.status() == ZLinkLocationWriteStatus.REJECTED_CONFLICT
                    && localGeneration > 0) {
                    ZLinkPeerLocation renewed = new ZLinkPeerLocation(
                        localRow.autoConnectType(), localRow.meshName(), localRow.nodeRid(),
                        localRow.role(), localRow.endpoint(), localRow.weight(), localRow.value(),
                        localRow.metadata(), localRow.capabilities(), localRow.ownerId(),
                        localGeneration, Instant.EPOCH);
                    return runtime.writePeerAsync(renewed, ZLinkLocationWriteIntent.RENEW)
                        .thenAccept(renewedResult -> {
                            boot(
                                "claim renew key=" + key
                                    + " status=" + renewedResult.status()
                                    + " generation=" + renewedResult.generation()
                                    + " local=" + localSummary());
                            localPublished = renewedResult.status() == ZLinkLocationWriteStatus.STORED;
                        });
                }
                return CompletableFuture.<Void>completedFuture(null);
            })
            .whenComplete((ignored, failure) -> {
                if (failure != null) {
                    boot(
                        "claim failure key=" + key
                            + " error=" + failure.getClass().getSimpleName()
                            + " local=" + localSummary());
                }
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

    private String localSummary() {
        return "type=" + local.type()
            + " mesh=" + local.meshName()
            + " role=" + local.role()
            + " rid=" + ridOf(local.nodeRid())
            + " endpoint=" + local.endpoint();
    }

    private static String peerSummary(ZLinkPeerLocation peer) {
        return "type=" + peer.autoConnectType()
            + " mesh=" + peer.meshName()
            + " role=" + peer.role()
            + " rid=" + ridOf(peer.nodeRid())
            + " endpoint=" + peer.endpoint()
            + " owner=" + peer.ownerId()
            + " generation=" + peer.generation();
    }

    private static String decisionSummary(ZLinkAutoConnectPlanner.PeerDecision decision) {
        if (!decision.shouldDial()) {
            return "skip reason=" + decision.skipReason();
        }
        return "dial target=" + targetSummary(decision.target());
    }

    private static String targetSummary(ZLinkAutoConnectPlanner.Target target) {
        return "key=" + target.key()
            + " role=" + target.role()
            + " rid=" + ridOf(target.nodeRid())
            + " endpoint=" + target.endpoint()
            + " owner=" + target.ownerId();
    }

    private static String ridOf(systems.zlink.contracts.core.RoutingId rid) {
        return ZLinkAutoConnectPlanner.hasRid(rid) ? rid.toHex() : "";
    }

    private static void boot(String step) {
    }
}
