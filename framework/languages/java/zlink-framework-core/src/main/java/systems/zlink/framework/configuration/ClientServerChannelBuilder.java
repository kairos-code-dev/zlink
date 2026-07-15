package systems.zlink.framework.configuration;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkSocketRuntimeOptions;

public interface ClientServerChannelBuilder {
    ClientServerChannelBuilder enableServer(String endpoint);

    ZLinkSocketRuntimeOptions configureServerSocket();

    ClientServerChannelBuilder setRoutingId(RoutingId routingId);

    ClientServerChannelBuilder enableClient();

    ClientServerChannelBuilder enableClient(String endpoint);

    ZLinkEndpointConnections clientConnections();

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
}
