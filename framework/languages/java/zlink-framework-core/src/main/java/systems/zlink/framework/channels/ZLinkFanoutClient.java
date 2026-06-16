package systems.zlink.framework.channels;

import systems.zlink.contracts.messaging.Message;

public interface ZLinkFanoutClient {
    ZLinkPublishCall publish(
        String channelName,
        String topic,
        Message message);
}
