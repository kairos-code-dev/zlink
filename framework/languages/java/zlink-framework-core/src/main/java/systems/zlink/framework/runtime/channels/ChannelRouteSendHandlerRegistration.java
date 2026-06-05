package systems.zlink.framework.runtime.channels;

import java.lang.reflect.Method;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;

record ChannelRouteSendHandlerRegistration<THandler extends ZLinkRouteSendHandler<TMessage>, TMessage>(
    Class<THandler> handlerType,
    Method handlerMethod,
    Class<TMessage> messageType,
    String packetName) {
    ChannelRouteSendHandlerRegistration(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName) {
        this(handlerType, null, messageType, packetName);
    }

    ChannelRouteSendHandlerRegistration<THandler, TMessage> withPacketName(String packetName) {
        return new ChannelRouteSendHandlerRegistration<>(
            handlerType,
            handlerMethod,
            messageType,
            packetName);
    }
}
