package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkRouteClient {
    ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        Message message);

    ZLinkRequestCall requestTo(
        String channelName,
        RoutingId target,
        Message message);
}
