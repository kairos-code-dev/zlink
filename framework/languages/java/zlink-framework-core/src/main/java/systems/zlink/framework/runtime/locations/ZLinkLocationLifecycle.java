package systems.zlink.framework.runtime.locations;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.spots.ZLinkSpotKind;

/** Tracks process-local materializations; durable ownership is stored only as authority. */
public final class ZLinkLocationLifecycle implements AutoCloseable {
    private final Set<String> spots = ConcurrentHashMap.newKeySet();
    private final Map<String, ActorRef> actors = new ConcurrentHashMap<>();
    private final Set<RoutingId> sessionRoutes = ConcurrentHashMap.newKeySet();

    public ZLinkLocationLifecycle(ZLinkLocationRuntime runtime) {
        java.util.Objects.requireNonNull(runtime, "runtime");
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimSpot(
        String meshName,
        String spotId,
        long spotGeneration,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        Runnable deactivate) {
        spots.add(spotId);
        return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimSpot(
        String meshName,
        String spotId,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        Runnable deactivate) {
        return claimSpot(meshName, spotId, 1L, spotType, nodeRid, spotKind, routeEndpoint, deactivate);
    }

    public CompletionStage<Void> releaseSpot(String meshName, String spotId) {
        spots.remove(spotId);
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimActor(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        actors.putIfAbsent(actorId, nullActorRef(actorId, nodeRid));
        return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
    }

    public CompletionStage<ZLinkLocationWriteStatus> takeoverActor(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        actors.put(actorId, nullActorRef(actorId, nodeRid));
        return CompletableFuture.completedFuture(ZLinkLocationWriteStatus.STORED);
    }

    public CompletionStage<Void> setActorRef(String actorType, String actorId, ActorRef actorRef) {
        actors.put(actorId, actorRef);
        return CompletableFuture.completedFuture(null);
    }

    public void abandonActor(String actorId) {
        actors.remove(actorId);
    }

    public CompletionStage<Void> notifyActorJoinedSpot(
        String actorType, String actorId, String meshName, String spotId) {
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<Void> notifyActorLeftSpot(String actorType, String actorId) {
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<Void> notifyActorMovedToEntrySpot(
        String actorType, String actorId, RoutingId nodeRid) {
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<Void> releaseActor(String actorType, String actorId) {
        actors.remove(actorId);
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<Void> bindActorSessionRoute(
        RoutingId sessionRid, String actorId, RoutingId ownerNodeRid) {
        sessionRoutes.add(sessionRid);
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<Void> removeActorSessionRoute(RoutingId sessionRid) {
        sessionRoutes.remove(sessionRid);
        return CompletableFuture.completedFuture(null);
    }

    boolean ownsActor(String actorType, String actorId) {
        return actors.containsKey(actorId);
    }

    @Override
    public void close() {
        spots.clear();
        actors.clear();
        sessionRoutes.clear();
    }

    private static ActorRef nullActorRef(String actorId, RoutingId nodeRid) {
        return new ActorRef(actorId, 1L, "local", nodeRid);
    }
}
