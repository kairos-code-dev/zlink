package systems.zlink.framework.runtime.channels;

import java.lang.reflect.Method;
import systems.zlink.framework.channels.ZLinkRequestHandler;

record ChannelRequestHandlerRegistration<THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>(
    Class<THandler> handlerType,
    Method handlerMethod,
    Class<TRequest> requestType,
    Class<TReply> replyType,
    String packetName) {
    ChannelRequestHandlerRegistration(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        String packetName) {
        this(handlerType, null, requestType, replyType, packetName);
    }

    ChannelRequestHandlerRegistration<THandler, TRequest, TReply> withPacketName(String packetName) {
        return new ChannelRequestHandlerRegistration<>(
            handlerType,
            handlerMethod,
            requestType,
            replyType,
            packetName);
    }
}
