package systems.zlink.framework.runtime.locations;

import java.util.Objects;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class ZLinkLocationLifecycle implements AutoCloseable {
    private final ZLinkLocationRuntime runtime;
    private final ZLinkSpotLocationLifecycle spots;
    private final ZLinkActorOwnershipCoordinator actors;
    private final ZLinkActorSessionRouteLifecycle actorSessionRoutes;
    private final java.util.function.BiConsumer<ZLinkLocationKind, String> ownershipLostListener =
        this::onOwnershipLost;

    public ZLinkLocationLifecycle(ZLinkLocationRuntime runtime) {
        this.runtime = Objects.requireNonNull(runtime, "runtime");
        this.spots = new ZLinkSpotLocationLifecycle(runtime);
        this.actors = new ZLinkActorOwnershipCoordinator(runtime);
        this.actorSessionRoutes = new ZLinkActorSessionRouteLifecycle(runtime);
        this.runtime.addOwnershipLostListener(ownershipLostListener);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimSpot(
        String meshName,
        RoutingId spotRid,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        Runnable deactivate) {
        return spots.claim(meshName, spotRid, spotType, nodeRid, spotKind, routeEndpoint, deactivate);
    }

    public CompletionStage<Void> releaseSpot(String meshName, RoutingId spotRid) {
        return spots.release(meshName, spotRid);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimActor(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return actors.claim(actorType, actorId, nodeRid, deactivate);
    }

    public CompletionStage<ZLinkLocationWriteStatus> takeoverActor(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return actors.takeover(actorType, actorId, nodeRid, deactivate);
    }

    public CompletionStage<Void> setActorRef(String actorType, String actorId, ActorRef actorRef) {
        return actors.setActorRef(actorType, actorId, actorRef);
    }

    public void abandonActor(String actorId) {
        actors.abandon(actorId);
    }

    public CompletionStage<Void> notifyActorJoinedSpot(
        String actorType,
        String actorId,
        String meshName,
        RoutingId spotRid) {
        return actors.notifyJoinedSpot(actorType, actorId, meshName, spotRid);
    }

    public CompletionStage<Void> notifyActorLeftSpot(String actorType, String actorId) {
        return actors.notifyLeftSpot(actorType, actorId);
    }

    public CompletionStage<Void> notifyActorMovedToEntrySpot(
        String actorType,
        String actorId,
        RoutingId nodeRid) {
        return actors.notifyMovedToEntrySpot(actorType, actorId, nodeRid);
    }

    public CompletionStage<Void> releaseActor(String actorType, String actorId) {
        return actors.release(actorType, actorId);
    }

    public CompletionStage<Void> bindActorSessionRoute(
        RoutingId sessionRid,
        String actorId,
        RoutingId ownerNodeRid) {
        return actorSessionRoutes.bind(sessionRid, actorId, ownerNodeRid);
    }

    public CompletionStage<Void> removeActorSessionRoute(RoutingId sessionRid) {
        return actorSessionRoutes.remove(sessionRid);
    }

    boolean ownsActor(String actorType, String actorId) {
        return actors.owns(actorType, actorId);
    }

    @Override
    public void close() {
        runtime.removeOwnershipLostListener(ownershipLostListener);
    }

    private void onOwnershipLost(ZLinkLocationKind kind, String canonicalKey) {
        if (kind == ZLinkLocationKind.ACTOR) {
            actors.onOwnershipLost(canonicalKey);
        } else if (kind == ZLinkLocationKind.SPOT) {
            spots.onOwnershipLost(canonicalKey);
        } else if (kind == ZLinkLocationKind.ROUTE) {
            actorSessionRoutes.onOwnershipLost(canonicalKey);
        }
    }
}
