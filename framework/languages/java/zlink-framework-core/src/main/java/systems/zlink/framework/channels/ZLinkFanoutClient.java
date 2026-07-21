package systems.zlink.framework.channels;

public interface ZLinkFanoutClient {
    ZLinkFanoutPublishCall publish(
        String channelName,
        Object message);
}
