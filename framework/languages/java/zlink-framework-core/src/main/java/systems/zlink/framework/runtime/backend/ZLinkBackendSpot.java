package systems.zlink.framework.runtime.backend;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendSpot extends ZLinkBackendObject {
    RoutingId routingId();

    void setRoutingId(RoutingId routingId);

    void setSubscription(String topic);

    ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode);

    ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode);

    boolean sendToChannel(String channelName, List<Message> parts, SendFlags flags);

    boolean requestToChannel(
        String channelName,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout);

    boolean publish(String topic, List<Message> parts, SendFlags flags);

    ZLinkBackendActorJoinRequest recvActorJoin(ZLinkBackendRecvMode mode);
}
