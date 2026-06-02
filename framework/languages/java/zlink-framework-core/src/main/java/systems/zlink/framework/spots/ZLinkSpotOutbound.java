package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;

public interface ZLinkSpotOutbound {
    <TMessage> ZLinkSendCall sendToSpot(
        RoutingId spotRid,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToSpot(
        RoutingId spotRid,
        TMessage request);

    <TEvent> ZLinkPublishCall publish(
        String topic,
        TEvent message);

    <TMessage> ZLinkSendCall sendToChannel(
        String channelName,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToChannel(
        String channelName,
        TMessage request);
}
