package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;

public interface ZLinkRouteClient {
    ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkSendCall sendToSpot(
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        Object message);

    ZLinkRouteRequestCall requestTo(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkRouteRequestCall requestToSpot(
        String channelName,
        RoutingId targetNode,
        RoutingId targetSpot,
        Object message);
}
