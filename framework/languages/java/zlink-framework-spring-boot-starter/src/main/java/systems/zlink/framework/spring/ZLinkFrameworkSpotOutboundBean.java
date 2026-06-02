package systems.zlink.framework.spring;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

final class ZLinkFrameworkSpotOutboundBean implements ZLinkSpotOutbound {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkSpotOutboundBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public <TMessage> ZLinkSendCall sendToSpot(RoutingId spotRid, TMessage message) {
        return lifecycle.spotOutbound().sendToSpot(spotRid, message);
    }

    @Override
    public <TMessage> ZLinkRequestCall requestToSpot(
        RoutingId spotRid,
        TMessage request) {
        return lifecycle.spotOutbound().requestToSpot(spotRid, request);
    }

    @Override
    public <TEvent> ZLinkPublishCall publish(String topic, TEvent message) {
        return lifecycle.spotOutbound().publish(topic, message);
    }

    @Override
    public <TMessage> ZLinkSendCall sendToChannel(
        String channelName,
        TMessage message) {
        return lifecycle.spotOutbound().sendToChannel(channelName, message);
    }

    @Override
    public <TMessage> ZLinkRequestCall requestToChannel(
        String channelName,
        TMessage request) {
        return lifecycle.spotOutbound().requestToChannel(channelName, request);
    }
}
