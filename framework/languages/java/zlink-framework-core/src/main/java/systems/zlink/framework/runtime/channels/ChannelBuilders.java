package systems.zlink.framework.runtime.channels;

import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.DealerMeshChannelBuilder;
import systems.zlink.framework.configuration.FanoutChannelBuilder;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkRouteConfigBuilder;

public final class ChannelBuilders {
    private ChannelBuilders() {
    }

    public static ClientServerChannelBuilder clientServer(ChannelRegistration registration) {
        return new ClientServer(registration);
    }

    public static FanoutChannelBuilder fanout(ChannelRegistration registration) {
        return new Fanout(registration);
    }

    public static DealerMeshChannelBuilder dealerMesh(ChannelRegistration registration) {
        return new DealerMesh(registration);
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
        public ClientServerChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
            return this;
        }

        @Override
        public <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType) {
            addSendHandler(handlerType, messageType, null);
        }

        @Override
        public <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType,
            String packetName) {
            registration.addSendHandler(new ChannelSendHandlerRegistration<>(
                handlerType,
                messageType,
                packetName));
        }

        @Override
        public <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
        void addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType) {
            addRequestHandler(handlerType, requestType, replyType, null);
        }

        @Override
        public <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
        void addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType,
            String packetName) {
            registration.addRequestHandler(new ChannelRequestHandlerRegistration<>(
                handlerType,
                requestType,
                replyType,
                packetName));
        }

        @Override
        public ClientServerChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName) {
            registration.enableSpotRouteEgress(targetSpotNodeChannelName);
            return this;
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
        public FanoutChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
            return this;
        }

        @Override
        public <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType) {
            addPublishHandler(handlerType, messageType, null);
        }

        @Override
        public <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType,
            String packetName) {
            registration.addPublishHandler(new ChannelPublishHandlerRegistration<>(
                handlerType,
                messageType,
                packetName));
        }

        @Override
        @SuppressWarnings({"unchecked", "rawtypes"})
        public FanoutChannelBuilder addPublishHandler(Class<?> handlerType) {
            addPublishHandler(handlerType, null);
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

    private record DealerMesh(ChannelRegistration registration) implements DealerMeshChannelBuilder {
        @Override
        public DealerMeshChannelBuilder enableClient() {
            registration.enableClient();
            return this;
        }

        @Override
        public DealerMeshChannelBuilder enableClient(String endpoint) {
            registration.enableClient();
            registration.addClientManualEndpoint(endpoint);
            return this;
        }

        @Override
        public DealerMeshChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
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
        public ZLinkRouteConfigBuilder configureRouting() {
            return registration::setRouteRoutingId;
        }

        @Override
        public RouteMeshChannelBuilder enableClient() {
            return this;
        }

        @Override
        public RouteMeshChannelBuilder enableClient(String endpoint) {
            registration.addRouteManualEndpoint(endpoint);
            return this;
        }

        @Override
        public RouteMeshChannelBuilder addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
            return this;
        }

        @Override
        public <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType) {
            addSendHandler(handlerType, messageType, null);
        }

        @Override
        public <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType,
            String packetName) {
            registration.addRouteSendHandler(new ChannelRouteSendHandlerRegistration<>(
                handlerType,
                messageType,
                packetName));
        }

        @Override
        public <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
        void addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType) {
            addRequestHandler(handlerType, requestType, replyType, null);
        }

        @Override
        public <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
        void addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType,
            String packetName) {
            registration.addRouteRequestHandler(new ChannelRouteRequestHandlerRegistration<>(
                handlerType,
                requestType,
                replyType,
                packetName));
        }

        @Override
        public RouteMeshChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName) {
            registration.enableSpotRouteEgress(targetSpotNodeChannelName);
            return this;
        }
    }
}
