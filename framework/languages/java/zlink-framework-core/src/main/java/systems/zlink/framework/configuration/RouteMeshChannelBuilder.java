package systems.zlink.framework.configuration;

import java.util.function.Consumer;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;

public interface RouteMeshChannelBuilder {
    void bind(String endpoint);

    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);

    void addHandlerGroup(String groupName);

    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        String packetName);

    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        String packetName);

    void enableSpotRouteEgress(String targetSpotNodeChannelName);
}
