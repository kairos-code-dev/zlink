package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;

public interface ZLinkBackendStreamSocket extends ZLinkBackendSocket {
    void setTlsServer(String certificatePath, String keyPath, boolean requireClientCertificate);

    void onPacket(ZLinkBackendStreamPacketHandler handler);

    void onTransportError(ZLinkBackendStreamErrorHandler handler);

    void startSessionService();

    boolean send(RoutingId routingId, List<Message> parts, SendFlags flags);

    boolean send(RoutingId routingId, String packetName, List<Message> parts, SendFlags flags);

    boolean send(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags);

    boolean reply(
        RoutingId routingId,
        long requestSeq,
        String packetName,
        List<Message> parts,
        SendFlags flags);

    boolean reply(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags);

    ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor);

    ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId);

    boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags);

    boolean relayBoundActor(
        RoutingId sessionRid,
        String actorId,
        ZLinkStreamHeader header,
        List<Message> parts,
        SendFlags flags);
}
