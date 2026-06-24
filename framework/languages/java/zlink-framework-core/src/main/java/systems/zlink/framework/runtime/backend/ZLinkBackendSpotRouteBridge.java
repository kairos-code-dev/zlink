package systems.zlink.framework.runtime.backend;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendSpotRouteBridge extends ZLinkBackendObject {
    void attachRouterChannel(
        String channelName,
        ZLinkBackendRouterSocket router);

    boolean send(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        SendFlags flags);

    boolean request(
        String channelName,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout);

    boolean handleRouterReceived(
        String channelName,
        RoutingId sourceNodeRid,
        long requestSeq,
        List<Message> parts);

    int drain();

    void close();
}
