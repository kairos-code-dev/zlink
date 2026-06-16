package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;

public interface ZLinkSpotOutbound {
    ZLinkSendCall sendToSpot(
        RoutingId spotRid,
        Object message);

    ZLinkRequestCall requestToSpot(
        RoutingId spotRid,
        Object request);

    ZLinkPublishCall publish(
        String topic,
        Object message);

    ZLinkSendCall sendToChannel(
        String channelName,
        Object message);

    ZLinkRequestCall requestToChannel(
        String channelName,
        Object request);
}
