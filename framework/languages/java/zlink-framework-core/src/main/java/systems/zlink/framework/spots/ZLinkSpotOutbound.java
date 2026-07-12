package systems.zlink.framework.spots;

import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;

public interface ZLinkSpotOutbound {
    ZLinkSendCall sendToSpot(
        SpotHandle spot,
        Object message);

    ZLinkYieldRequestCall requestToSpot(
        SpotHandle spot,
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
