package systems.zlink.framework.runtime.channels;

import java.util.function.Consumer;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.configuration.ChannelServerCapabilityBuilder;
import systems.zlink.framework.configuration.ChannelPublisherCapabilityBuilder;
import systems.zlink.framework.configuration.ClientCapabilityBuilder;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.DealerMeshChannelBuilder;
import systems.zlink.framework.configuration.FanoutChannelBuilder;
import systems.zlink.framework.configuration.ManualEndpointListBuilder;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.SubscriberCapabilityBuilder;
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
        public void enableServer() {
            registration.enableServer();
        }

        @Override
        public void enableServer(Consumer<ChannelServerCapabilityBuilder> configure) {
            enableServer();
            configure.accept(registration::addServerBind);
        }

        @Override
        public void enableClient() {
            registration.enableClient();
        }

        @Override
        public void enableClient(Consumer<ClientCapabilityBuilder> configure) {
            enableClient();
            configure.accept(clientConfigure ->
                clientConfigure.accept((ManualEndpointListBuilder) registration::addClientManualEndpoint));
        }

        @Override
        public void addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
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
            Class<TReply> replyType,
            String packetName) {
            registration.addRequestHandler(new ChannelRequestHandlerRegistration<>(
                handlerType,
                requestType,
                replyType,
                packetName));
        }

        @Override
        public void enableSpotRouteEgress(String targetSpotNodeChannelName) {
            registration.enableSpotRouteEgress(targetSpotNodeChannelName);
        }
    }

    private record Fanout(ChannelRegistration registration) implements FanoutChannelBuilder {
        @Override
        public void enablePublisher() {
            registration.enablePublisher();
        }

        @Override
        public void enablePublisher(Consumer<ChannelPublisherCapabilityBuilder> configure) {
            enablePublisher();
            configure.accept(registration::addPublisherBind);
        }

        @Override
        public void enableSubscriber() {
            registration.enableSubscriber();
        }

        @Override
        public void enableSubscriber(Consumer<SubscriberCapabilityBuilder> configure) {
            enableSubscriber();
            configure.accept(subscriberConfigure ->
                subscriberConfigure.accept((ManualEndpointListBuilder) registration::addSubscriberManualEndpoint));
        }

        @Override
        public void addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
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
        public void addPublishHandler(Class<?> handlerType, String packetName) {
            registration.addPublishHandler(new ChannelPublishHandlerRegistration(
                handlerType,
                String.class,
                packetName));
        }
    }

    private record DealerMesh(ChannelRegistration registration) implements DealerMeshChannelBuilder {
        @Override
        public void enableClient() {
            registration.enableClient();
        }

        @Override
        public void enableClient(Consumer<ClientCapabilityBuilder> configure) {
            enableClient();
            configure.accept(clientConfigure ->
                clientConfigure.accept((ManualEndpointListBuilder) registration::addClientManualEndpoint));
        }

        @Override
        public void addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
        }
    }

    private record RouteMesh(ChannelRegistration registration) implements RouteMeshChannelBuilder {
        @Override
        public void bind(String endpoint) {
            registration.addRouteBind(endpoint);
        }

        @Override
        public void configureRouting(Consumer<ZLinkRouteConfigBuilder> configure) {
            configure.accept(registration::setRouteRoutingId);
        }

        @Override
        public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(registration::addRouteManualEndpoint);
        }

        @Override
        public void addHandlerGroup(String groupName) {
            registration.addHandlerGroup(groupName);
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
            Class<TReply> replyType,
            String packetName) {
            registration.addRouteRequestHandler(new ChannelRouteRequestHandlerRegistration<>(
                handlerType,
                requestType,
                replyType,
                packetName));
        }

        @Override
        public void enableSpotRouteEgress(String targetSpotNodeChannelName) {
            registration.enableSpotRouteEgress(targetSpotNodeChannelName);
        }
    }
}
