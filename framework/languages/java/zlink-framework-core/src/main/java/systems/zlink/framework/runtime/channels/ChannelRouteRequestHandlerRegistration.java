package systems.zlink.framework.runtime.channels;

import java.lang.reflect.Method;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;

record ChannelRouteRequestHandlerRegistration<
    THandler extends ZLinkRouteRequestHandler<TRequest, TReply>,
    TRequest,
    TReply>(
    Class<THandler> handlerType,
    Method handlerMethod,
    Class<TRequest> requestType,
    Class<TReply> replyType,
    String packetName) {
    ChannelRouteRequestHandlerRegistration(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        String packetName) {
        this(handlerType, null, requestType, replyType, packetName);
    }

    ChannelRouteRequestHandlerRegistration<THandler, TRequest, TReply> withPacketName(String packetName) {
        return new ChannelRouteRequestHandlerRegistration<>(
            handlerType,
            handlerMethod,
            requestType,
            replyType,
            packetName);
    }
}
