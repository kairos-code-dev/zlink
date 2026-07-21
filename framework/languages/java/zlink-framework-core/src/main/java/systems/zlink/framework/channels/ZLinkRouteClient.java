package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.SpotHandle;

public interface ZLinkRouteClient {
    ZLinkSendCall sendToChannel(
        String channelName,
        Object message);

    ZLinkRequestCall requestToChannel(
        String channelName,
        Object request);

    ZLinkSendCall sendToNode(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkSendCall sendToSpot(
        SpotHandle spot,
        Object message);

    ZLinkRequestCall requestToNode(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkRequestCall requestToSpot(
        SpotHandle spot,
        Object message);
}
