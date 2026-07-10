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

    public CompletionStage<ZLinkLocationWriteStatus> claimSpotAsync(
        String meshName,
        RoutingId spotRid,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        Runnable deactivate) {
        return spots.claimAsync(meshName, spotRid, spotType, nodeRid, spotKind, routeEndpoint, deactivate);
    }

    public CompletionStage<Void> releaseSpotAsync(String meshName, RoutingId spotRid) {
        return spots.releaseAsync(meshName, spotRid);
    }

    public CompletionStage<ZLinkLocationWriteStatus> claimActorAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return actors.claimAsync(actorType, actorId, nodeRid, deactivate);
    }

    public CompletionStage<ZLinkLocationWriteStatus> takeoverActorAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid,
        Runnable deactivate) {
        return actors.takeoverAsync(actorType, actorId, nodeRid, deactivate);
    }

    public CompletionStage<Void> setActorRefAsync(String actorType, String actorId, ActorRef actorRef) {
        return actors.setActorRefAsync(actorType, actorId, actorRef);
    }

    public CompletionStage<Void> notifyActorJoinedSpotAsync(
        String actorType,
        String actorId,
        String meshName,
        RoutingId spotRid) {
        return actors.notifyJoinedSpotAsync(actorType, actorId, meshName, spotRid);
    }

    public CompletionStage<Void> notifyActorLeftSpotAsync(String actorType, String actorId) {
        return actors.notifyLeftSpotAsync(actorType, actorId);
    }

    public CompletionStage<Void> notifyActorMovedToEntrySpotAsync(
        String actorType,
        String actorId,
        RoutingId nodeRid) {
        return actors.notifyMovedToEntrySpotAsync(actorType, actorId, nodeRid);
    }

    public CompletionStage<Void> releaseActorAsync(String actorType, String actorId) {
        return actors.releaseAsync(actorType, actorId);
    }

    public CompletionStage<Void> bindActorSessionRouteAsync(
        RoutingId sessionRid,
        String actorId,
        RoutingId ownerNodeRid) {
        return actorSessionRoutes.bindAsync(sessionRid, actorId, ownerNodeRid);
    }

    public CompletionStage<Void> removeActorSessionRouteAsync(RoutingId sessionRid) {
        return actorSessionRoutes.removeAsync(sessionRid);
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
