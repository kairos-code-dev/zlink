package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;

public interface ZLinkSpotOutbound {
    ZLinkSendCall sendToSpot(
        RoutingId spotRid,
        Object message);

    ZLinkYieldRequestCall requestToSpot(
        RoutingId spotRid,
        Object request);

    ZLinkPublishCall publish(
        String topic,
        Object message);

    ZLinkSendCall sendToChannel(
        String channelName,
        Object message);

    ZLinkYieldRequestCall requestToChannel(
        String channelName,
        Object request);
}
