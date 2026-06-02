package systems.zlink.framework.runtime;

import systems.zlink.framework.channels.ZLinkRequestHandler;

record ChannelRequestHandlerRegistration<THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>(
    Class<THandler> handlerType,
    Class<TRequest> requestType,
    Class<TReply> replyType,
    String packetName) {
}
