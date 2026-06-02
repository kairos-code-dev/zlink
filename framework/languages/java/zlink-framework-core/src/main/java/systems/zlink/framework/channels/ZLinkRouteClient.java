package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;

public interface ZLinkRouteClient {
    <TMessage> ZLinkSendCall sendTo(
        String channelName,
        RoutingId target,
        TMessage message);

    <TMessage> ZLinkRequestCall requestTo(
        String channelName,
        RoutingId target,
        TMessage message);
}
