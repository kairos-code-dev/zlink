package systems.zlink.framework.runtime.backend;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendSpot extends ZLinkBackendObject {
    RoutingId routingId();

    default long lifecycleGeneration() {
        return 0L;
    }

    void setRoutingId(RoutingId routingId);

    void setSubscription(String topic);

    ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode);

    ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode);

    default void rememberSpotAuthority(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long objectGeneration,
        long authorityOwnerGeneration) {
    }

    boolean publish(
        String channelName,
        String topic,
        List<Message> parts,
        SendFlags flags);

    default boolean publish(
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        if (metadata == null || metadata.length == 0) {
            return publish(channelName, topic, parts, flags);
        }
        throw new UnsupportedOperationException("Spot publish metadata is unavailable");
    }

    boolean sendToSpot(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        List<Message> parts,
        SendFlags flags);

    default boolean sendToSpot(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        if (metadata == null || metadata.length == 0) {
            return sendToSpot(
                targetNodeRid, spotRid, spotGeneration, parts, flags);
        }
        throw new UnsupportedOperationException("Spot send metadata is unavailable");
    }

    boolean requestToSpot(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout);

    default boolean requestToSpot(
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        if (metadata == null || metadata.length == 0) {
            return requestToSpot(
                targetNodeRid,
                spotRid,
                spotGeneration,
                parts,
                callback,
                flags,
                timeout);
        }
        throw new UnsupportedOperationException("Spot request metadata is unavailable");
    }
    void onDispatchEvent(ZLinkBackendSpotDispatchHandler handler);

    ZLinkBackendActorJoinRequest recvActorJoin(ZLinkBackendRecvMode mode);

    void replyActorJoin(
        ZLinkBackendActorJoinRequest request,
        int joinResultCode,
        List<Message> parts);

    ZLinkBackendActorLifecycleEvent recvActorLifecycle(ZLinkBackendRecvMode mode);
}
