package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

final class ZLinkFrameworkSpotOutboundBean implements ZLinkSpotOutbound {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkSpotOutboundBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public ZLinkSendCall sendToSpot(RoutingId spotRid, Object message) {
        return lifecycle.spotOutbound().sendToSpot(spotRid, message);
    }

    @Override
    public ZLinkYieldRequestCall requestToSpot(
        RoutingId spotRid,
        Object request) {
        return lifecycle.spotOutbound().requestToSpot(spotRid, request);
    }

    @Override
    public ZLinkPublishCall publish(String topic, Object message) {
        return lifecycle.spotOutbound().publish(topic, message);
    }

    @Override
    public ZLinkSendCall sendToChannel(
        String channelName,
        Object message) {
        return lifecycle.spotOutbound().sendToChannel(channelName, message);
    }

    @Override
    public ZLinkYieldRequestCall requestToChannel(
        String channelName,
        Object request) {
        return lifecycle.spotOutbound().requestToChannel(channelName, request);
    }
}
