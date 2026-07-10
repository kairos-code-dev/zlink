package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.spots.ZLinkSpot;

final class ZLinkActorContextState {
    private ZLinkActor actor;
    private final String actorId;
    private ZLinkBackendActorRef actorRef;
    private ZLinkBoundSession boundSession;
    private long sessionBindingToken;
    private RoutingId boundSessionSourceNodeRid;
    private RoutingId boundSessionSourceSessionRid;
    private RoutingId spotRid;
    private ZLinkSpot<?> spot;
    private boolean joined;
    private boolean destroying;

    ZLinkActorContextState(ZLinkBackendActorRef actorRef) {
        this.actorId = actorRef.actorId();
        this.actorRef = actorRef;
    }

    ZLinkActor actor() {
        return actor;
    }

    void setActor(ZLinkActor actor) {
        this.actor = actor;
    }

    String actorId() {
        return actorId;
    }

    ZLinkBackendActorRef actorRef() {
        return actorRef;
    }

    RoutingId boundSessionSourceNodeRid() {
        return boundSessionSourceNodeRid;
    }

    RoutingId boundSessionSourceSessionRid() {
        return boundSessionSourceSessionRid;
    }

    RoutingId boundSessionSourceSessionRid(long bindingToken) {
        return bindingToken == sessionBindingToken ? boundSessionSourceSessionRid : null;
    }

    RoutingId spotRid() {
        return spotRid;
    }

    boolean joined() {
        return joined;
    }

    ZLinkBoundSession requireBoundSession() {
        if (boundSession == null) {
            throw new ZLinkConfigurationException("actor has no bound session: " + actorId);
        }
        return boundSession;
    }

    ZLinkSpot<?> requireSpot() {
        if (spot == null) {
            throw new ZLinkConfigurationException("actor has not joined a user Spot");
        }
        return spot;
    }

    void markJoined(
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        this.actorRef = actorRef;
        this.spotRid = spotRid;
        this.spot = spot;
        this.joined = true;
    }

    void markMovedToEntrySpot(
        ZLinkBackendActorRef actorRef,
        RoutingId entrySpotNodeRid) {
        this.actorRef = actorRef;
        updateNativeBoundSessionActorRef(actorRef);
        this.spotRid = entrySpotNodeRid;
        this.spot = null;
        this.joined = true;
    }

    void markNativeActorRef(
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        this.actorRef = actorRef;
        this.boundSessionSourceNodeRid = sourceNodeRid;
        this.boundSessionSourceSessionRid = sourceSessionRid;
    }

    void markLeft() {
        spotRid = null;
        spot = null;
        joined = false;
    }

    long bindSession(
        ZLinkBoundSession boundSession,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        boundSessionSourceNodeRid = sourceNodeRid;
        boundSessionSourceSessionRid = sourceSessionRid;
        sessionBindingToken++;
        this.boundSession = boundSession;
        return sessionBindingToken;
    }

    boolean clearBoundSession(long bindingToken) {
        if (bindingToken == sessionBindingToken) {
            boundSession = null;
            boundSessionSourceNodeRid = null;
            boundSessionSourceSessionRid = null;
            return true;
        }
        return false;
    }

    CompletionStage<Void> rebindNativeActor(
        ZLinkBackendActorRef targetActor,
        Duration timeout) {
        if (boundSession instanceof ZLinkBoundSessionRuntime runtime) {
            return runtime.rebindNativeActor(targetActor, timeout);
        }
        updateNativeBoundSessionActorRef(targetActor);
        return CompletableFuture.completedFuture(null);
    }

    void updateNativeBoundSessionActorRef(ZLinkBackendActorRef targetActor) {
        if (boundSession instanceof ZLinkNativeBoundSessionRuntime runtime) {
            runtime.updateActorRef(targetActor);
        }
    }

    boolean hasBoundSession() {
        return boundSession != null;
    }

    boolean hasBoundSession(
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        return boundSession != null
            && java.util.Objects.equals(boundSessionSourceNodeRid, sourceNodeRid)
            && java.util.Objects.equals(boundSessionSourceSessionRid, sourceSessionRid);
    }

    CompletionStage<Boolean> sendBoundSessionFrame(byte[] frameBytes) {
        if (boundSession instanceof ZLinkNativeBoundSessionRuntime runtime) {
            return runtime.sendFrame(frameBytes).thenApply(ignored -> true);
        }
        if (boundSession instanceof ZLinkRoutedBoundSessionRuntime runtime) {
            return runtime.sendFrame(frameBytes).thenApply(ignored -> true);
        }
        return CompletableFuture.completedFuture(false);
    }

    CompletionStage<Void> disconnectBoundSessionForDestroy() {
        ZLinkBoundSession current = boundSession;
        if (current == null) {
            return CompletableFuture.completedFuture(null);
        }
        return current.disconnect();
    }

    void clearAfterDestroy() {
        actorRef = null;
        boundSession = null;
        boundSessionSourceNodeRid = null;
        boundSessionSourceSessionRid = null;
        sessionBindingToken++;
        spotRid = null;
        spot = null;
        joined = false;
        destroying = false;
    }

    ZLinkBackendActorRef beginDestroy(RoutingId entryNodeRid, String actorId) {
        if (destroying) {
            return null;
        }
        if (actorRef == null) {
            throw new ZLinkConfigurationException(
                "actor does not have a native Actor ref: " + actorId);
        }
        if (spot != null) {
            throw new ZLinkConfigurationException(
                "actor must leave its current Spot before destroy: " + actorId);
        }
        if (!actorRef.nodeRid().equals(entryNodeRid)) {
            throw new ZLinkConfigurationException(
                "actor is not owned by this Entry Spot: " + actorId);
        }

        destroying = true;
        return actorRef;
    }

    void resetDestroying() {
        destroying = false;
    }
}
