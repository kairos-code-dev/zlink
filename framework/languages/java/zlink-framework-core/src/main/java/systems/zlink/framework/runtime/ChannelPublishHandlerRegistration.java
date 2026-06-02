package systems.zlink.framework.runtime;

import systems.zlink.framework.channels.ZLinkPublishHandler;

record ChannelPublishHandlerRegistration<THandler extends ZLinkPublishHandler<TMessage>, TMessage>(
    Class<THandler> handlerType,
    Class<TMessage> messageType,
    String packetName) {
}
