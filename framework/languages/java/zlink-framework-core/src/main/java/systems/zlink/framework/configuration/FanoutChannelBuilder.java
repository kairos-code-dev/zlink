package systems.zlink.framework.configuration;

import systems.zlink.framework.channels.ZLinkPublishHandler;

public interface FanoutChannelBuilder {
    FanoutChannelBuilder enablePublisher(String endpoint);

    FanoutChannelBuilder enableSubscriber();

    FanoutChannelBuilder enableSubscriber(String endpoint);

    FanoutChannelBuilder addHandlerGroup(String groupName);

    <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);

    <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName);

    FanoutChannelBuilder addPublishHandler(Class<?> handlerType);

    FanoutChannelBuilder addPublishHandler(Class<?> handlerType, String packetName);
}
