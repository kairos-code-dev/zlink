package systems.zlink.framework.spots;

import systems.zlink.framework.channels.ZLinkPublishCall;

public interface ZLinkSpotPublisherClient {
    <TEvent> ZLinkPublishCall publishSpot(
        String channelName,
        String topic,
        TEvent message);
}
