package systems.zlink.framework.configuration;

import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;

public interface ClientServerChannelBuilder {
    ClientServerChannelBuilder enableServer(String endpoint);

    ClientServerChannelBuilder enableClient();

    ClientServerChannelBuilder enableClient(String endpoint);

    ClientServerChannelBuilder addHandlerGroup(String groupName);

    <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);

    <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName);

    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);

    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        String packetName);

    ClientServerChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName);
}
