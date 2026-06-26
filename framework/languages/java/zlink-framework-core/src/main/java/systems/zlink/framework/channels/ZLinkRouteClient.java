package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;

public interface ZLinkRouteClient {
    ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkRouteRequestCall requestTo(
        String channelName,
        RoutingId target,
        Object message);
}
