package systems.zlink.framework.runtime.internal.backend;

import systems.zlink.framework.runtime.backend.ZLinkBackendObject;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;

import java.util.List;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorTransferId;
import systems.zlink.contracts.service.spot.ActorTransferPrepare;
import systems.zlink.contracts.service.spot.ActorTransferPrepareResult;
import systems.zlink.contracts.service.spot.ActorTransferToken;
import systems.zlink.contracts.service.spot.PrepareActorTransferResult;
import systems.zlink.contracts.sockets.SendFlags;


public interface ZLinkInternalSpotNode extends ZLinkBackendObject {
    RoutingId routingId();

    void setRoutingId(RoutingId routingId);

    void setPublisherRoutingId(RoutingId routingId);

    void setSubscriberRoutingId(RoutingId routingId);

    void setRouterBind(String endpoint);

    void setPubBind(String endpoint);

    void connectPeer(String endpoint);

    void connectPeer(RoutingId peerRid, String endpoint);

    void disconnectPeer(String endpoint);

    void disconnectPeer(RoutingId peerRid);

    default void setApplicationReceiver(ZLinkMeshApplicationReceiver receiver) {
        // Alternate backends may not support process-local Node direct dispatch.
    }

    default java.util.Optional<Integer> submitLocalNodeSend(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts) {
        return java.util.Optional.empty();
    }

    default java.util.Optional<Integer> classifyNodeSendTarget(
        RoutingId targetNodeRid) {
        return java.util.Optional.empty();
    }

    ZLinkBackendSpotRouteBridge createRouteBridge();

    ZLinkBackendSpot createSpot();

    default ZLinkBackendSpot createSpot(String spotId) {
        ZLinkBackendSpot spot = createSpot();
        spot.setRoutingId(spotId);
        return spot;
    }

    default ZLinkBackendSpot createSpot(
        String spotId,
        long objectGeneration) {
        throw new UnsupportedOperationException(
            "Exact-generation Spot creation is unavailable");
    }

    ZLinkBackendSpot entrySpot();

    default boolean sendToNode(
        RoutingId targetNodeRid,
        List<Message> parts,
        SendFlags flags) {
        throw new UnsupportedOperationException("MeshNode node send is unavailable");
    }

    default boolean sendToNode(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        if (metadata == null || metadata.length == 0) {
            return sendToNode(targetNodeRid, parts, flags);
        }
        throw new UnsupportedOperationException("MeshNode node metadata is unavailable");
    }

    default boolean requestToNode(
        RoutingId targetNodeRid,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        throw new UnsupportedOperationException("MeshNode node request is unavailable");
    }

    default boolean requestToNode(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        if (metadata == null || metadata.length == 0) {
            return requestToNode(targetNodeRid, parts, callback, flags, timeout);
        }
        throw new UnsupportedOperationException("MeshNode node request metadata is unavailable");
    }

    default boolean sendToChannel(
        String channelName,
        List<Message> parts,
        SendFlags flags) {
        throw new UnsupportedOperationException("MeshNode channel send is unavailable");
    }

    default boolean sendToChannel(
        String channelName,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        if (metadata == null || metadata.length == 0) {
            return sendToChannel(channelName, parts, flags);
        }
        throw new UnsupportedOperationException("MeshNode channel metadata is unavailable");
    }

    default boolean requestToChannel(
        String channelName,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        throw new UnsupportedOperationException("MeshNode channel request is unavailable");
    }

    default boolean requestToChannel(
        String channelName,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        if (metadata == null || metadata.length == 0) {
            return requestToChannel(channelName, parts, callback, flags, timeout);
        }
        throw new UnsupportedOperationException(
            "MeshNode channel request metadata is unavailable");
    }

    default void publish(
        String channelName,
        String topic,
        List<Message> parts,
        SendFlags flags) {
        throw new UnsupportedOperationException("MeshNode publish is unavailable");
    }

    default void publish(
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        if (metadata == null || metadata.length == 0) {
            publish(channelName, topic, parts, flags);
            return;
        }
        throw new UnsupportedOperationException("MeshNode publish metadata is unavailable");
    }

    ZLinkBackendActorRef createActor(String actorId, Message createRequest);

    ZLinkBackendActorRef actorLookup(String actorId);

    default void rememberActorAuthority(
        ZLinkBackendActorRef actor,
        long authorityOwnerGeneration) {
    }

    CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        String targetSpotId,
        List<Message> parts,
        Duration timeout);

    default CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        String targetSpotId,
        long targetSpotGeneration,
        List<Message> parts,
        Duration timeout) {
        return joinActor(actor, targetNodeRid, targetSpotId, parts, timeout);
    }

    CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        Message request,
        Duration timeout);

    CompletionStage<List<Message>> leaveActor(
        ZLinkBackendActorRef actor,
        String currentSpotId,
        Duration timeout);

    CompletionStage<Void> destroyActor(
        ZLinkBackendActorRef actor,
        Duration timeout);

    default PrepareActorTransferResult prepareActorTransfer(
        ActorTransferPrepare prepare, Duration timeout) {
        throw new UnsupportedOperationException("Core actor transfer is unavailable");
    }

    default void commitActorTransfer(
        ActorTransferToken token, long newMembershipEpoch) {
        throw new UnsupportedOperationException("Core actor transfer is unavailable");
    }

    default void activateActorTransfer(ActorTransferToken token) {
        throw new UnsupportedOperationException("Core actor transfer is unavailable");
    }

    default void abortActorTransfer(ActorTransferToken token) {
        throw new UnsupportedOperationException("Core actor transfer is unavailable");
    }

    default long actorMembershipEpoch(String actorId) {
        return 0L;
    }

    default void registerTransferredActor(
        ZLinkBackendActorRef actor, String spotId, long membershipEpoch) {
        throw new UnsupportedOperationException("Core actor transfer is unavailable");
    }

    boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags);

    void replyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long requestId,
        int flags,
        List<Message> parts);

    boolean sendToActor(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags);

    CompletionStage<List<Message>> requestToActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        SendFlags flags,
        Duration timeout);

    boolean forwardActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        List<Message> parts,
        SendFlags flags);

    default byte[] encodeLocalSessionActorAccepted(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long sourceBindingGeneration,
        long sourceSessionSequence,
        long requestSequence,
        String packetName,
        java.util.Map<String, String> metadata,
        byte[] payload) {
        return new byte[0];
    }

    void bindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid);

    void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout);

}
