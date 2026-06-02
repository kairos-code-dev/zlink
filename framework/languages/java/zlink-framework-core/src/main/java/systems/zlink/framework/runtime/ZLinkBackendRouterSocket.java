package systems.zlink.framework.runtime;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendRouterSocket extends ZLinkBackendConnectableSocket {
    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void setRoutingId(RoutingId routingId);

    ZLinkBackendReceived recv(ZLinkBackendRecvMode mode);

    boolean send(RoutingId routingId, List<Message> parts, SendFlags flags);

    boolean request(
        RoutingId routingId,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout);

    void reply(RoutingId routingId, long requestSeq, List<Message> parts);
}
