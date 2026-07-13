package systems.zlink.framework.runtime.channels;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.FanoutChannelBuilder;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkEndpointConnections;

public final class ChannelBuilders {
    private ChannelBuilders() {
    }

    public static ClientServerChannelBuilder clientServer(ChannelRegistration registration) {
        return new ClientServer(registration);
    }

    public static FanoutChannelBuilder fanout(ChannelRegistration registration) {
        return new Fanout(registration);
    }

    public static RouteMeshChannelBuilder routeMesh(ChannelRegistration registration) {
        return new RouteMesh(registration);
    }

    private record ClientServer(ChannelRegistration registration) implements ClientServerChannelBuilder {
        @Override
        public ClientServerChannelBuilder enableServer(String endpoint) {
            registration.enableServer();
            registration.addServerBind(endpoint);
            return this;
        }

        @Override
        public ClientServerChannelBuilder setRoutingId(RoutingId routingId) {
            registration.setRoutingId(routingId);
            return this;
        }

        @Override
        public ClientServerChannelBuilder enableClient() {
            registration.enableClient();
            return this;
        }

        @Override
        public ClientServerChannelBuilder enableClient(String endpoint) {
            registration.enableClient();
            registration.addClientManualEndpoint(endpoint);
            return this;
        }

        @Override
        public ZLinkEndpointConnections clientConnections() {
            return registration.clientConnections();
        }

        @Override
        public ClientServerChannelBuilder setDefaultRequestTimeout(Duration timeout) {
            registration.setDefaultRequestTimeout(timeout);
            return this;
        }

        @Override
        public ClientServerChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
            return this;
        }

        @Override
        public void addSendHandler(
            Class<?> handlerType,
            Class<?> messageType) {
            addSendHandler(handlerType, messageType, null);
        }

        @Override
        public void addSendHandler(
            Class<?> handlerType,
            Class<?> messageType,
            String packetName) {
            registration.addSendHandler(new ChannelSendHandlerRegistration(
                handlerType,
                messageType,
                packetName));
        }

        @Override
        public void addRequestHandler(
            Class<?> handlerType,
            Class<?> requestType,
            Class<?> replyType) {
            addRequestHandler(handlerType, requestType, replyType, null);
        }

        @Override
        public void addRequestHandler(
            Class<?> handlerType,
            Class<?> requestType,
            Class<?> replyType,
            String packetName) {
            registration.addRequestHandler(new ChannelRequestHandlerRegistration(
                handlerType,
                requestType,
                replyType,
                packetName));
        }

    }

    private record Fanout(ChannelRegistration registration) implements FanoutChannelBuilder {
        @Override
        public FanoutChannelBuilder enablePublisher(String endpoint) {
            registration.enablePublisher();
            registration.addPublisherBind(endpoint);
            return this;
        }

        @Override
        public FanoutChannelBuilder setRoutingId(RoutingId routingId) {
            registration.setRoutingId(routingId);
            return this;
        }

        @Override
        public FanoutChannelBuilder enableSubscriber() {
            registration.enableSubscriber();
            return this;
        }

        @Override
        public FanoutChannelBuilder enableSubscriber(String endpoint) {
            registration.enableSubscriber();
            registration.addSubscriberManualEndpoint(endpoint);
            return this;
        }

        @Override
        public ZLinkEndpointConnections subscriberConnections() {
            return registration.subscriberConnections();
        }

        @Override
        public FanoutChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
            return this;
        }

        @Override
        public void addPublishHandler(
            Class<?> handlerType,
            Class<?> messageType) {
            addPublishHandler(handlerType, messageType, null);
        }

        @Override
        public void addPublishHandler(
            Class<?> handlerType,
            Class<?> messageType,
            String packetName) {
            registration.addPublishHandler(new ChannelPublishHandlerRegistration(
                handlerType,
                messageType,
                packetName));
        }

        @Override
        @SuppressWarnings({"unchecked", "rawtypes"})
        public FanoutChannelBuilder addPublishHandler(Class<?> handlerType) {
            addPublishHandler(handlerType, (String) null);
            return this;
        }

        @Override
        @SuppressWarnings({"unchecked", "rawtypes"})
        public FanoutChannelBuilder addPublishHandler(Class<?> handlerType, String packetName) {
            registration.addPublishHandler(new ChannelPublishHandlerRegistration(
                handlerType,
                String.class,
                packetName));
            return this;
        }
    }

    private record RouteMesh(ChannelRegistration registration) implements RouteMeshChannelBuilder {
        @Override
        public RouteMeshChannelBuilder enableServer(String endpoint) {
            registration.addRouteBind(endpoint);
            return this;
        }

        @Override
        public RouteMeshChannelBuilder setRoutingId(RoutingId routingId) {
            registration.setRouteRoutingId(routingId);
            return this;
        }

        @Override
        public RouteMeshChannelBuilder setDefaultRequestTimeout(Duration timeout) {
            registration.setDefaultRequestTimeout(timeout);
            return this;
        }

        @Override
        public RouteMeshChannelBuilder enableClient() {
            registration.enableClient();
            return this;
        }

        @Override
        public RouteMeshChannelBuilder enableClient(String endpoint) {
            registration.enableClient();
            registration.addRouteManualEndpoint(endpoint);
            return this;
        }

        @Override
        public ZLinkEndpointConnections clientConnections() {
            return registration.routeConnections();
        }

        @Override
        public RouteMeshChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
            return this;
        }

        @Override
        public void addSendHandler(
            Class<?> handlerType,
            Class<?> messageType) {
            addSendHandler(handlerType, messageType, null);
        }

        @Override
        public void addSendHandler(
            Class<?> handlerType,
            Class<?> messageType,
            String packetName) {
            registration.addRouteSendHandler(new ChannelRouteSendHandlerRegistration(
                handlerType,
                messageType,
                packetName));
        }

        @Override
        public void addRequestHandler(
            Class<?> handlerType,
            Class<?> requestType,
            Class<?> replyType) {
            addRequestHandler(handlerType, requestType, replyType, null);
        }

        @Override
        public void addRequestHandler(
            Class<?> handlerType,
            Class<?> requestType,
            Class<?> replyType,
            String packetName) {
            registration.addRouteRequestHandler(new ChannelRouteRequestHandlerRegistration(
                handlerType,
                requestType,
                replyType,
                packetName));
        }

    }

}
