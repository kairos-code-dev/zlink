package systems.zlink.framework.configuration;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkMeshNodeBuilder {
    ZLinkMeshChannelBuilder channelName(String channelName);

    ZLinkMeshNodeBuilder listen(String endpoint);

    ZLinkMeshNodeBuilder setRoutingId(RoutingId routingId);

    ZLinkMeshNodeBuilder setPlacementWeight(int weight);

    ZLinkMeshNodeBuilder useAllocatedRoutingId(int slotCount);

    ZLinkMeshNodeBuilder useAllocatedRoutingId(int slotCount, String routingIdPrefix);

    ZLinkMeshNodeBuilder setRoutingIdAllocationGroup(String groupName);

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

    ZLinkEntrySpotOptions configureEntrySpot();

    ZLinkMeshNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType);

    ZLinkMeshNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType);

    ZLinkMeshNodeBuilder addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);

    ZLinkMeshNodeBuilder addActorTransferAdapter(
        String actorType,
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType);
}
