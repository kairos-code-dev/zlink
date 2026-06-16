package systems.zlink.framework.channels;

public interface ZLinkFanoutClient {
    ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message);
}
