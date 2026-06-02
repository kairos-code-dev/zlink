package systems.zlink.framework.runtime;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendStreamSocket extends ZLinkBackendSocket {
    void onPacket(ZLinkBackendStreamPacketHandler handler);

    void onTransportError(ZLinkBackendStreamErrorHandler handler);

    boolean send(RoutingId routingId, List<Message> parts, SendFlags flags);

    void attachActorGateway(ZLinkBackendSpotNode node);

    ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor);

    ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId);

    boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags);
}
