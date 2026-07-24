package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import java.util.Optional;
import java.time.Duration;
import java.util.function.Consumer;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshNodeMonitor;
import systems.zlink.contracts.service.spot.PeerChannels;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;
import systems.zlink.framework.runtime.service.ZLinkServiceM6BWireCodec;

public interface ZLinkInternalMeshNode extends ZLinkBackendObject {
    void setBind(String endpoint);

    void addChannel(String channelName);

    void setChannelWeight(String channelName, int weight);

    default int placementWeight() {
        return 100;
    }

    default void setPlacementWeight(int weight) {
        // Optional for test and alternate backends that do not publish
        // Framework-owned service descriptors.
    }

    default long maxMessageSize() {
        return 0;
    }

    default void setMaxMessageSize(long value) {
        throw new UnsupportedOperationException("MeshNode max message size is not available");
    }

    default void setRouterHighWaterMark(int value) {
        // Optional for test and alternate backends that do not expose Core
        // RouteMesh admission yet.
    }

    default void setRouterSendTimeout(Duration value) {
        // Optional for test and alternate backends that do not expose Core
        // RouteMesh admission yet.
    }

    default void setRouterPendingAdmissionCapacity(int value) {
        // Optional for test and alternate backends that do not expose bounded
        // asynchronous RouteMesh admission yet.
    }

    default void setMailboxMessageBudget(long value) {
        // Optional for test and alternate backends that do not expose Core
        // mailbox admission yet.
    }

    void setRoutingId(RoutingId routingId);

    void start();

    long connectPeer(String endpoint);

    long connectPeer(String endpoint, RoutingId expectedRoutingId);

    default void removePeerConnection(long connectionIntentId) {
        // Optional for alternate and test backends that do not retain connection intents.
    }

    MeshNodeStatus status();

    List<MeshPeerEntry> peers();

    default PeerChannels peerChannels(RoutingId peerRid, long lifecycleGeneration) {
        return new PeerChannels(List.of(), List.of());
    }

    default java.util.Map<String, Integer> channelWeights() {
        return java.util.Map.of();
    }

    default MeshNodeMonitor openMonitor() {
        throw new UnsupportedOperationException("MeshNode monitor is not available");
    }

    List<Long> connectionIntentIds();

    void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver);

    default void setApplicationReceiver(ZLinkMeshApplicationReceiver receiver) {
        // Alternate backends may not support process-local Node direct dispatch.
    }

    default ZLinkInternalSpotNode spotNode() {
        throw new UnsupportedOperationException("MeshNode Spot backend is not available");
    }

    default Optional<RoutingId> selectPlacementTarget() {
        return Optional.empty();
    }

    default void setUserSpotOperationHandler(
        UserSpotOperationHandler handler) {
        // Alternate backends may not yet own Framework service operations.
    }

    default void setActorCreateOperationHandler(
        ActorCreateOperationHandler handler) {
        // Alternate backends may not yet own Framework Actor creation.
    }

    default CompletionStage<ActorCreateResponse> requestActorCreate(
        RoutingId targetNodeRid,
        ActorCreateIntent intent,
        Duration timeout) {
        return java.util.concurrent.CompletableFuture.failedFuture(
            new UnsupportedOperationException(
                "Remote Actor create is unavailable"));
    }

    default CompletionStage<UserSpotCreateResponse>
        requestUserSpotCreate(
            RoutingId targetNodeRid,
            UserSpotCreateIntent intent,
            Duration timeout) {
        return java.util.concurrent.CompletableFuture.failedFuture(
            new UnsupportedOperationException(
                "Remote User Spot create is unavailable"));
    }

    default CompletionStage<UserSpotCloseResponse>
        requestUserSpotClose(
            RoutingId targetNodeRid,
            UserSpotCloseIntent intent,
            Duration timeout) {
        return java.util.concurrent.CompletableFuture.failedFuture(
            new UnsupportedOperationException(
                "Remote User Spot close is unavailable"));
    }

    default void rememberSpotAuthority(
        SpotAuthorityRoute route) {
        // Alternate backends may resolve the durable route on each call.
    }

    default void forgetSpotAuthority(
        SpotAuthorityRoute route) {
        // Alternate backends may resolve the durable route on each call.
    }

    default void registerInstanceIntent(
        String stableType,
        ZLinkServiceM6BWireCodec.InstanceRouteFence route) {
        // Alternate backends may materialize Instance Spot elsewhere.
    }

    default void forgetInstanceIntent(
        ZLinkServiceM6BWireCodec.InstanceRouteFence route) {
        // Alternate backends may materialize Instance Spot elsewhere.
    }

    interface UserSpotOperationHandler {
        CompletionStage<UserSpotCreateResponse> create(
            UserSpotCreateRequest request);

        CompletionStage<UserSpotCloseResponse> close(
            UserSpotCloseRequest request);
    }

    interface ActorCreateOperationHandler {
        CompletionStage<ActorCreateResponse> create(
            ActorCreateRequest request);
    }

    record UserSpotCreateIntent(
        String spotId,
        String stableType,
        ZLinkServiceM6BWireCodec.ReservationFence reservation,
        long deadlineUnixMs) {
        public UserSpotCreateIntent {
            java.util.Objects.requireNonNull(spotId, "spotId");
            java.util.Objects.requireNonNull(stableType, "stableType");
            java.util.Objects.requireNonNull(
                reservation, "reservation");
        }
    }

    record ActorCreateIntent(
        String actorId,
        String stableType,
        ZLinkServiceM6BWireCodec.ReservationFence reservation,
        long operationHigh,
        long operationLow,
        long deadlineUnixMs) {
        public ActorCreateIntent {
            java.util.Objects.requireNonNull(actorId, "actorId");
            java.util.Objects.requireNonNull(stableType, "stableType");
            java.util.Objects.requireNonNull(
                reservation, "reservation");
            if (operationHigh == 0 && operationLow == 0) {
                throw new IllegalArgumentException(
                    "operationId must not be zero");
            }
        }
    }

    record UserSpotCloseIntent(
        ZLinkServiceM6BWireCodec.UserSpotCloseFence target,
        long deadlineUnixMs) {
        public UserSpotCloseIntent {
            java.util.Objects.requireNonNull(target, "target");
        }
    }

    record UserSpotCreateRequest(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        long operationHigh,
        long operationLow,
        UserSpotCreateIntent intent) {
    }

    record ActorCreateRequest(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        long operationHigh,
        long operationLow,
        ActorCreateIntent intent) {
    }

    record UserSpotCloseRequest(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        long operationHigh,
        long operationLow,
        UserSpotCloseIntent intent) {
    }

    record UserSpotCreateResponse(
        ZLinkServiceM6BWireCodec.UserSpotCreateResult result,
        String spotId,
        long objectGeneration,
        List<systems.zlink.contracts.messaging.Message>
            applicationReply) {
        public UserSpotCreateResponse {
            java.util.Objects.requireNonNull(result, "result");
            java.util.Objects.requireNonNull(spotId, "spotId");
            applicationReply = List.copyOf(
                java.util.Objects.requireNonNull(
                    applicationReply, "applicationReply"));
        }
    }

    record ActorCreateResponse(byte[] terminalEnvelope) {
        public ActorCreateResponse {
            terminalEnvelope = java.util.Objects.requireNonNull(
                terminalEnvelope, "terminalEnvelope").clone();
        }

        @Override
        public byte[] terminalEnvelope() {
            return terminalEnvelope.clone();
        }
    }

    record UserSpotCloseResponse(boolean closed) {
    }

    record SpotAuthorityRoute(
        String spotId,
        long objectGeneration,
        RoutingId targetNodeRid,
        long targetNodeGeneration,
        long authorityOwnerGeneration,
        long ownerLeaseGeneration,
        String ownerId,
        String meshName,
        String storeVersion) {
        public SpotAuthorityRoute {
            java.util.Objects.requireNonNull(spotId, "spotId");
            java.util.Objects.requireNonNull(
                targetNodeRid, "targetNodeRid");
            java.util.Objects.requireNonNull(ownerId, "ownerId");
            java.util.Objects.requireNonNull(meshName, "meshName");
            java.util.Objects.requireNonNull(
                storeVersion, "storeVersion");
        }
    }
}
