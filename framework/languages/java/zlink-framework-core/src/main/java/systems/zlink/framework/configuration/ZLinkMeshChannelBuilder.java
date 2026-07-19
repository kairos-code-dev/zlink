package systems.zlink.framework.configuration;

import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;

public interface ZLinkMeshChannelBuilder {
    ZLinkMeshChannelBuilder setWeight(int weight);

    ZLinkMeshChannelBuilder addHandlerGroup(String groupName);

    <THandler extends ZLinkSendHandler<TMessage>, TMessage>
    ZLinkMeshChannelBuilder addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);

    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    ZLinkMeshChannelBuilder addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);
}
