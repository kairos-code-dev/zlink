package systems.zlink.framework.runtime.channels;

import java.lang.reflect.Method;
import systems.zlink.framework.channels.ZLinkPublishHandler;

record ChannelPublishHandlerRegistration<THandler extends ZLinkPublishHandler<TMessage>, TMessage>(
    Class<THandler> handlerType,
    Method handlerMethod,
    Class<TMessage> messageType,
    String packetName) {
    ChannelPublishHandlerRegistration(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName) {
        this(handlerType, null, messageType, packetName);
    }
}
