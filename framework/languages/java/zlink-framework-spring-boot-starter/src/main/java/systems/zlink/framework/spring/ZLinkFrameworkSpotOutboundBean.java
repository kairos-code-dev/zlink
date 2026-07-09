package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

final class ZLinkFrameworkSpotOutboundBean implements ZLinkSpotOutbound {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkSpotOutboundBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public ZLinkSendCall sendToSpot(SpotRef spotRef, Object message) {
        return lifecycle.spotOutbound().sendToSpot(spotRef, message);
    }

    @Override
    public ZLinkYieldRequestCall requestToSpot(
        SpotRef spotRef,
        Object request) {
        return lifecycle.spotOutbound().requestToSpot(spotRef, request);
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
