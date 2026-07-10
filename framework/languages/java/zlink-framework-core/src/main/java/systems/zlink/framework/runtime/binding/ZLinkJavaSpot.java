package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotActorLifecycleEvent;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendTopicMessage;

record ZLinkJavaSpot(Spot spot) implements ZLinkBackendSpot {
    @Override public String name() { return "spot"; }
    @Override public RoutingId routingId() { return spot.getRoutingId(); }
    @Override public void setRoutingId(RoutingId routingId) { spot.setRoutingId(routingId); }
    @Override public void setSubscription(String topic) { spot.setSubscription(topic); }
    @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) {
        try (TopicMessage result = new TopicMessage()) {
            return spot.subscribe(result, ZLinkJavaSocketSupport.map(mode))
                ? new ZLinkBackendTopicMessage(
                    result.getRoutingId(),
                    result.topic(),
                    ZLinkJavaBackendCodec.copyParts(result.parts()))
                : null;
        }
    }
    @Override public ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode) {
        Received result = new Received();
        if (ZLinkJavaSocketSupport.recvOrNoData(
            () -> spot.recvRouted(result, ZLinkJavaSocketSupport.map(mode)))) {
            return ZLinkJavaSocketSupport.fromReceived(result);
        }
        result.close();
        return null;
    }
    @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(spot.publish(topic), parts, flags);
    }
    @Override public boolean sendToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(spot.sendToSpot(targetNodeRid, spotRid), parts, flags);
    }
    @Override public boolean requestToSpot(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        return ZLinkJavaSocketSupport.submitRequest(
            spot.requestToSpot(targetNodeRid, spotRid),
            parts,
            callback,
            flags,
            timeout);
    }
    @Override public void onDispatchEvent(ZLinkBackendSpotDispatchHandler handler) {
        spot.setDispatchHandler(info -> handler.handle(ZLinkJavaSpotCodec.fromSpotDispatchInfo(info)));
    }
    @Override public ZLinkBackendActorJoinRequest recvActorJoin(ZLinkBackendRecvMode mode) {
        ActorJoinRequest request = spot.recvActorJoin(ZLinkJavaSocketSupport.map(mode));
        if (request == null) {
            return null;
        }
        return ZLinkJavaSpotCodec.fromActorJoinRequest(request);
    }
    @Override public void replyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        List<Message> parts) {
        ActorJoinRequest nativeRequest = (ActorJoinRequest) request.nativeRequest();
        var operation = spot.replyActorJoin(nativeRequest, joinResultCode);
        for (Message part : parts) {
            operation.message(part);
        }
        operation.submit();
    }
    @Override public ZLinkBackendActorLifecycleEvent recvActorLifecycle(
        ZLinkBackendRecvMode mode) {
        SpotActorLifecycleEvent event = spot.recvActorLifecycle(ZLinkJavaSocketSupport.map(mode));
        return event == null ? null : ZLinkJavaSpotCodec.fromActorLifecycleEvent(event);
    }
    @Override public void close() { spot.close(); }
}
