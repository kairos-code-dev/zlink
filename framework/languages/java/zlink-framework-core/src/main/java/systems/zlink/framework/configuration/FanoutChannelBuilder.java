package systems.zlink.framework.configuration;

import java.util.function.Consumer;
import systems.zlink.framework.channels.ZLinkPublishHandler;

public interface FanoutChannelBuilder {
    void enablePublisher();

    void enablePublisher(Consumer<ChannelPublisherCapabilityBuilder> configure);

    void enableSubscriber();

    void enableSubscriber(Consumer<SubscriberCapabilityBuilder> configure);

    void addHandlerGroup(String groupName);

    <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName);

    void addPublishHandler(Class<?> handlerType, String packetName);
}
