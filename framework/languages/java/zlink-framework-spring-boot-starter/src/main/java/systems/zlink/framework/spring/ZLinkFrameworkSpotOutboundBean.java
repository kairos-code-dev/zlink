package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
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
    public ZLinkSendCall sendToSpot(RoutingId spotRid, Message message) {
        return lifecycle.spotOutbound().sendToSpot(spotRid, message);
    }

    @Override
    public ZLinkRequestCall requestToSpot(
        RoutingId spotRid,
        Message request) {
        return lifecycle.spotOutbound().requestToSpot(spotRid, request);
    }

    @Override
    public ZLinkPublishCall publish(String topic, Message message) {
        return lifecycle.spotOutbound().publish(topic, message);
    }

    @Override
    public ZLinkSendCall sendToChannel(
        String channelName,
        Message message) {
        return lifecycle.spotOutbound().sendToChannel(channelName, message);
    }

    @Override
    public ZLinkRequestCall requestToChannel(
        String channelName,
        Message request) {
        return lifecycle.spotOutbound().requestToChannel(channelName, request);
    }
}
