package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkSpotAddress;

public interface ZLinkRouteClient {
    ZLinkSendCall sendToNode(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkSendCall sendToSpot(
        String channelName,
        ZLinkSpotAddress address,
        Object message);

    ZLinkRequestCall requestToNode(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkRequestCall requestToSpot(
        String channelName,
        ZLinkSpotAddress address,
        Object message);
}
