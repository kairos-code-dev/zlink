package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.channels.ZLinkRouteSendHandler;

record ChannelRouteSendHandlerRegistration<THandler extends ZLinkRouteSendHandler<TMessage>, TMessage>(
    Class<THandler> handlerType,
    Class<TMessage> messageType,
    String packetName) {
    ChannelRouteSendHandlerRegistration<THandler, TMessage> withPacketName(String packetName) {
        return new ChannelRouteSendHandlerRegistration<>(
            handlerType,
            messageType,
            packetName);
    }
}
