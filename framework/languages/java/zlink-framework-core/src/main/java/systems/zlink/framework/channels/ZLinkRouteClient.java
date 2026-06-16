package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;

public interface ZLinkRouteClient {
    ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkRequestCall requestTo(
        String channelName,
        RoutingId target,
        Object message);
}
