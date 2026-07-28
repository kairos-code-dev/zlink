package systems.zlink.framework.configuration;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;

public interface ZLinkMeshNodeBuilder {
    ZLinkMeshChannelBuilder channelName(String channelName);

    ZLinkMeshNodeBuilder listen(String endpoint);

    ZLinkMeshNodeBuilder listen();

    ZLinkMeshNodeBuilder listen(int port);

    ZLinkMeshNodeBuilder setBindHost(String host);

    ZLinkMeshNodeBuilder setAdvertiseHost(String host);

    ZLinkMeshNodeBuilder setRoutingId(
        systems.zlink.contracts.core.RoutingId routingId);

    ZLinkMeshNodeBuilder setRoutingIdPrefix(String prefix);

    ZLinkMeshNodeBuilder setPlacementWeight(int weight);

    ZLinkMeshNodeBuilder setActorCapacity(int maxActors);

    ZLinkMeshNodeBuilder setSpotCapacity(int maxSpots);

    ZLinkMeshNodeBuilder setActivationConcurrency(
        int maxConcurrentActivations);

    ZLinkMeshNodeSocketConfig configureRouterSocket();

    ZLinkSpotPublisherConfig configureSpotPublisher();

    ZLinkMeshPeerConnections peerConnections();

    ZLinkMeshNodeBuilder setDefaultRequestTimeout(Duration timeout);

    ZLinkMeshObjectRoleBuilder objects();

    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage>
    ZLinkMeshNodeBuilder addRouteSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);

    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    ZLinkMeshNodeBuilder addRouteRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);

}
