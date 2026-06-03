package systems.zlink.framework.runtime.backend;

import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendStreamSocket extends ZLinkBackendSocket {
    void onPacket(ZLinkBackendStreamPacketHandler handler);

    void onTransportError(ZLinkBackendStreamErrorHandler handler);

    boolean send(RoutingId routingId, List<Message> parts, SendFlags flags);

    boolean send(RoutingId routingId, String packetName, List<Message> parts, SendFlags flags);

    boolean reply(
        RoutingId routingId,
        long requestSeq,
        String packetName,
        List<Message> parts,
        SendFlags flags);

    void attachActorGateway(ZLinkBackendSpotNode node);

    ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor);

    ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId);

    boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags);

    boolean relayBoundActor(
        RoutingId sessionRid,
        String actorId,
        String packetName,
        Optional<Long> requestSeq,
        List<Message> parts,
        SendFlags flags);
}
