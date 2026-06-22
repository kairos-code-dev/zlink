package systems.zlink.framework.configuration;

import systems.zlink.contracts.core.RoutingId;

public interface ClientServerChannelBuilder {
    ClientServerChannelBuilder enableServer(String endpoint);

    ClientServerChannelBuilder serverRoutingId(RoutingId routingId);

    ClientServerChannelBuilder enableClient();

    ClientServerChannelBuilder enableClient(String endpoint);

    ClientServerChannelBuilder setDefaultRequestTimeout(java.time.Duration timeout);

    ClientServerChannelBuilder addHandlerGroup(String groupName);

    void addSendHandler(
        Class<?> handlerType,
        Class<?> messageType);

    void addSendHandler(
        Class<?> handlerType,
        Class<?> messageType,
        String packetName);

    void addRequestHandler(
        Class<?> handlerType,
        Class<?> requestType,
        Class<?> replyType);

    void addRequestHandler(
        Class<?> handlerType,
        Class<?> requestType,
        Class<?> replyType,
        String packetName);

    ClientServerChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName);
}
