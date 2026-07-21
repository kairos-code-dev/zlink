package systems.zlink.framework.spots;

import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkRequestCall;

public interface ZLinkSpotOutbound {
    ZLinkSendCall sendToSpot(
        SpotHandle spot,
        Object message);

    ZLinkRequestCall requestToSpot(
        SpotHandle spot,
        Object request);

    ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message);

    ZLinkSendCall sendToChannel(
        String channelName,
        Object message);

    ZLinkRequestCall requestToChannel(
        String channelName,
        Object request);
}
