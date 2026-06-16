package systems.zlink.framework.configuration;

import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;

public interface RouteMeshChannelBuilder {
    RouteMeshChannelBuilder enableServer(String endpoint);

    ZLinkRouteConfigBuilder configureRouting();

    RouteMeshChannelBuilder enableClient();

    RouteMeshChannelBuilder enableClient(String endpoint);

    RouteMeshChannelBuilder addHandlerGroup(String groupName);

    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);

    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName);

    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);

    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        String packetName);

    RouteMeshChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName);
}
