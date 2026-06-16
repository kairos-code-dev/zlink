package systems.zlink.framework.spots;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkPublishCall;

public interface ZLinkSpotPublisherClient {
    ZLinkPublishCall publishSpot(
        String channelName,
        String topic,
        Message message);
}
