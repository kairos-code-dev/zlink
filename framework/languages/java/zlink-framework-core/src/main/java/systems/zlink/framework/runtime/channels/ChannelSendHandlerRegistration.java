package systems.zlink.framework.runtime.channels;

import java.lang.reflect.Method;
import systems.zlink.framework.channels.ZLinkSendHandler;

record ChannelSendHandlerRegistration<THandler extends ZLinkSendHandler<TMessage>, TMessage>(
    Class<THandler> handlerType,
    Method handlerMethod,
    Class<TMessage> messageType,
    String packetName) {
    ChannelSendHandlerRegistration(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName) {
        this(handlerType, null, messageType, packetName);
    }
}
