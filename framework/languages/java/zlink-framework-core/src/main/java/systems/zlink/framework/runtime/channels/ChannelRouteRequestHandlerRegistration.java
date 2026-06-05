package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.channels.ZLinkRouteRequestHandler;

record ChannelRouteRequestHandlerRegistration<
    THandler extends ZLinkRouteRequestHandler<TRequest, TReply>,
    TRequest,
    TReply>(
    Class<THandler> handlerType,
    Class<TRequest> requestType,
    Class<TReply> replyType,
    String packetName) {
    ChannelRouteRequestHandlerRegistration<THandler, TRequest, TReply> withPacketName(String packetName) {
        return new ChannelRouteRequestHandlerRegistration<>(
            handlerType,
            requestType,
            replyType,
            packetName);
    }
}
