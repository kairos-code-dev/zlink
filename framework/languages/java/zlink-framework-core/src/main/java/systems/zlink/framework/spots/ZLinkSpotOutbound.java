package systems.zlink.framework.spots;

import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.locations.SpotRef;

public interface ZLinkSpotOutbound {
    ZLinkSendCall sendToSpot(
        SpotRef spotRef,
        Object message);

    ZLinkYieldRequestCall requestToSpot(
        SpotRef spotRef,
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
