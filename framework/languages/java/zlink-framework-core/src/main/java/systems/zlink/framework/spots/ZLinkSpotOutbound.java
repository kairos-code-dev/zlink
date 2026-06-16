package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;

public interface ZLinkSpotOutbound {
    ZLinkSendCall sendToSpot(
        RoutingId spotRid,
        Message message);

    ZLinkRequestCall requestToSpot(
        RoutingId spotRid,
        Message request);

    ZLinkPublishCall publish(
        String topic,
        Message message);

    ZLinkSendCall sendToChannel(
        String channelName,
        Message message);

    ZLinkRequestCall requestToChannel(
        String channelName,
        Message request);
}
