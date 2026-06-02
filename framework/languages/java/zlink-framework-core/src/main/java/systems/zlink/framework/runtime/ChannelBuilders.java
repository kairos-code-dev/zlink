package systems.zlink.framework.runtime;

import java.util.function.Consumer;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.configuration.ChannelServerCapabilityBuilder;
import systems.zlink.framework.configuration.ClientCapabilityBuilder;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.ManualEndpointListBuilder;

final class ChannelBuilders {
    private ChannelBuilders() {
    }

    static ClientServerChannelBuilder clientServer(ChannelRegistration registration) {
        return new ClientServer(registration);
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
        }

        @Override
        public <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType,
            Class<TMessage> messageType,
            String packetName) {
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
        }
    }
}
