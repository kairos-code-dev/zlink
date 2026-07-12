package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.service.spot.ActorRecvInfo;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;

final class ZLinkJavaSpotNode implements ZLinkInternalSpotNode {
    private final SpotNode spotNode;

    ZLinkJavaSpotNode(SpotNode spotNode) {
        this.spotNode = spotNode;
    }

    @Override public String name() { return "spotNode"; }
    @Override public RoutingId routingId() { return spotNode.getRoutingId(); }
    @Override public void setRoutingId(RoutingId routingId) { spotNode.setRoutingId(routingId); }
    @Override public void setPublisherRoutingId(RoutingId routingId) { spotNode.setPublisherRoutingId(routingId); }
    @Override public void setSubscriberRoutingId(RoutingId routingId) { spotNode.setSubscriberRoutingId(routingId); }
    @Override public void setRouterBind(String endpoint) { spotNode.setRouterBind(endpoint); }
    @Override public void setPubBind(String endpoint) { spotNode.setPubBind(endpoint); }
    @Override public void connectPeer(String endpoint) { spotNode.connectPeer(endpoint); }
    @Override public void connectPeer(RoutingId peerRid, String endpoint) { spotNode.connectPeerRid(peerRid, endpoint); }
    @Override public void disconnectPeer(String endpoint) { spotNode.disconnectPeer(endpoint); }
    @Override public void disconnectPeer(RoutingId peerRid) { spotNode.disconnectPeerRid(peerRid); }
    @Override public ZLinkBackendSpotRouteBridge createRouteBridge() { return new ZLinkJavaSpotRouteBridge(spotNode.createRouteBridge()); }
    @Override public ZLinkBackendSpot createSpot() { return new ZLinkJavaSpot(spotNode.createSpot()); }
    @Override public ZLinkBackendSpot entrySpot() { return new ZLinkJavaSpot(spotNode.entrySpot()); }
    @Override public ZLinkBackendActorRef createActor(String actorId, Message createRequest) {
        Actor actor = spotNode.createActor(actorId, createRequest);
        return ZLinkJavaSpotCodec.fromActorRef(actor.ref());
    }
    @Override public ZLinkBackendActorRef actorLookup(String actorId) {
        return ZLinkJavaSpotCodec.fromActorRef(spotNode.actorLookup(actorId));
    }
    @Override public CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        Duration timeout) {
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("actor join request must contain at least one part");
        }
        ActorJoinSubmitOperation operation = spotNode.joinActor(
            toNative(actor),
            targetNodeRid,
            targetSpotRid)
            .message(parts.get(0));
        for (int i = 1; i < parts.size(); i++) {
            Message part = parts.get(i);
            operation = operation.message(part);
        }
        return operation.timeout(timeout)
            .submit()
            .thenApply(ZLinkJavaSpotNode::fromActorJoinCompletion);
    }
    @Override public CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        Message request,
        Duration timeout) {
        return spotNode.joinActorEntrySpot(toNative(actor), targetNodeRid, request)
            .timeout(timeout)
            .submit()
            .thenApply(completion -> {
                var result = completion.result();
                return new ZLinkBackendActorJoinEntrySpotResult(
                    ZLinkBackendRequestResult.valueOf(result.result().name()),
                    result.joinResultCode(),
                    ZLinkJavaSpotCodec.fromActorRef(result.actor()),
                    result.targetNodeRid(),
                    result.joinedSpotRid(),
                    result.joinEpoch(),
                    result.flags(),
                    completion.replyParts());
            });
    }
    @Override public CompletionStage<List<Message>> leaveActor(
        ZLinkBackendActorRef actor,
        RoutingId currentSpotRid,
        Duration timeout) {
        return spotNode.leaveActor(toNative(actor), currentSpotRid)
            .timeout(timeout)
            .submit();
    }
    @Override public CompletionStage<Void> destroyActor(ZLinkBackendActorRef actor, Duration timeout) {
        return spotNode.destroyActor(toNative(actor))
            .timeout(timeout)
            .submit()
            .thenApply(ignored -> null);
    }
    @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(
            spotNode.sendActorBoundSession(toNative(actor)),
            parts,
            flags);
    }
    @Override public void replyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long requestId,
        int flags,
        List<Message> parts) {
        spotNode.replyActorNoBind(
            new ActorRecvInfo(
                toNative(actor),
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags),
            parts,
            RequestResult.OK);
    }
    @Override public boolean sendToActor(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(
            spotNode.sendToActor(toNative(actor)),
            parts,
            flags);
    }
    @Override public CompletionStage<List<Message>> requestToActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        SendFlags flags,
        Duration timeout) {
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("actor request must contain at least one part");
        }
        var submit = spotNode.requestToActor(toNative(actor))
            .message(parts.get(0))
            .timeout(timeout);
        for (int i = 1; i < parts.size(); i++) {
            submit = submit.message(parts.get(i));
        }
        return submit.submit()
            .thenApply(reply -> reply.stream().map(Message::from).toList());
    }
    @Override public boolean forwardActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        List<Message> parts,
        SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(
            spotNode.forwardActorBoundSession(
                toNative(actor),
                sourceNodeRid,
                sourceSessionRid),
            parts,
            flags);
    }
    @Override public void bindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        spotNode.bindRemoteActorBoundSession(toNative(actor), sourceNodeRid, sourceSessionRid);
    }
    @Override public void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout) {
        spotNode.closeActorBoundSession(toNative(actor), timeout);
    }
    @Override public SpotNodeStatus status() { return spotNode.status(); }
    @Override public List<SpotNodePeerEntry> peers() { return spotNode.peers(); }
    @Override public List<SpotNodeSubjectEntry> subjects() { return spotNode.subjects(); }
    @Override public void close() { spotNode.close(); }

    private static ActorRef toNative(ZLinkBackendActorRef actor) {
        return new ActorRef(actor.nodeRid(), actor.actorId(), actor.generation());
    }

    private static ZLinkBackendActorJoinResult fromActorJoinCompletion(
        ActorJoinCompletion completion) {
        var result = completion.result();
        return new ZLinkBackendActorJoinResult(
            ZLinkBackendRequestResult.valueOf(result.result().name()),
            result.joinResultCode(),
            ZLinkJavaSpotCodec.fromActorRef(result.actor()),
            result.joinedSpotRid(),
            result.joinEpoch(),
            result.flags(),
            completion.replyParts());
    }
}
