package systems.zlink.framework.runtime.spots;

import java.util.function.Supplier;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

final class ZLinkSpotOutboundScope {
    private final ThreadLocal<DefaultSpotOutbound> current = new ThreadLocal<>();
    private final ZLinkSpotOutbound ambient = new ZLinkAmbientSpotOutbound(this);

    ZLinkSpotOutbound ambient() {
        return ambient;
    }

    <T> T run(DefaultSpotOutbound outbound, Supplier<T> action) {
        DefaultSpotOutbound previous = current.get();
        current.set(outbound);
        try {
            return action.get();
        } finally {
            if (previous == null) {
                current.remove();
            } else {
                current.set(previous);
            }
        }
    }

    DefaultSpotOutbound requireCurrent() {
        DefaultSpotOutbound outbound = current.get();
        if (outbound == null) {
            throw new ZLinkConfigurationException(
                "ZLinkSpotOutbound can only be used inside an active Spot callback");
        }
        return outbound;
    }

}

final class ZLinkAmbientSpotOutbound implements ZLinkSpotOutbound {
    private final ZLinkSpotOutboundScope scope;

    ZLinkAmbientSpotOutbound(ZLinkSpotOutboundScope scope) {
        this.scope = scope;
    }

    @Override
    public ZLinkSendCall sendToSpot(SpotRef spotRef, Object message) {
        return scope.requireCurrent().sendToSpot(spotRef, message);
    }

    @Override
    public ZLinkYieldRequestCall requestToSpot(SpotRef spotRef, Object request) {
        return scope.requireCurrent().requestToSpot(spotRef, request);
    }

    @Override
    public ZLinkPublishCall publish(String topic, Object message) {
        return scope.requireCurrent().publish(topic, message);
    }

    @Override
    public ZLinkSendCall sendToChannel(String channelName, Object message) {
        return scope.requireCurrent().sendToChannel(channelName, message);
    }

    @Override
    public ZLinkYieldRequestCall requestToChannel(String channelName, Object request) {
        return scope.requireCurrent().requestToChannel(channelName, request);
    }
}
