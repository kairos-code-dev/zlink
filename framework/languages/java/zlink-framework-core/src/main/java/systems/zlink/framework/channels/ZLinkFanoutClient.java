package systems.zlink.framework.channels;

public interface ZLinkFanoutClient {
    <TMessage> ZLinkPublishCall publish(
        String channelName,
        String topic,
        TMessage message);
}
