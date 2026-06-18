package systems.zlink.framework.configuration;

public interface FanoutChannelBuilder {
    FanoutChannelBuilder enablePublisher(String endpoint);

    FanoutChannelBuilder enableSubscriber();

    FanoutChannelBuilder enableSubscriber(String endpoint);

    FanoutChannelBuilder addHandlerGroup(String groupName);

    void addPublishHandler(
        Class<?> handlerType,
        Class<?> messageType);

    void addPublishHandler(
        Class<?> handlerType,
        Class<?> messageType,
        String packetName);

    FanoutChannelBuilder addPublishHandler(Class<?> handlerType);

    FanoutChannelBuilder addPublishHandler(Class<?> handlerType, String packetName);
}
