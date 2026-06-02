package systems.zlink.framework.runtime;

import java.util.function.Consumer;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.configuration.ChannelPublisherCapabilityBuilder;
import systems.zlink.framework.configuration.ChannelServerCapabilityBuilder;
import systems.zlink.framework.configuration.ClientCapabilityBuilder;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.DealerMeshChannelBuilder;
import systems.zlink.framework.configuration.FanoutChannelBuilder;
import systems.zlink.framework.configuration.ManualEndpointListBuilder;
import systems.zlink.framework.configuration.RegistryBuilder;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.SpotPubSubCapabilityBuilder;
import systems.zlink.framework.configuration.SpotRouterCapabilityBuilder;
import systems.zlink.framework.configuration.SubscriberCapabilityBuilder;
import systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder;
import systems.zlink.framework.configuration.ZLinkDiagnosticsOptions;
import systems.zlink.framework.configuration.ZLinkDispatchMode;
import systems.zlink.framework.configuration.ZLinkDispatchOptions;
import systems.zlink.framework.configuration.ZLinkLogLevel;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder;
import systems.zlink.framework.configuration.ZLinkRegistrySpotRemoteAddressesOptions;
import systems.zlink.framework.configuration.ZLinkRouteConfigBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.configuration.ZLinkStreamNodeBuilder;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchOptions;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.streams.ZLinkSession;

final class NoopBuilders {
    static final ZLinkCodecRegistryBuilder CODECS = new ZLinkCodecRegistryBuilder() {
        @Override public void addJson() { }
        @Override public void addMessagePack() { }
        @Override public void addProtobuf() { }
    };
    static final ZLinkMetadataPolicyBuilder METADATA = key -> { };
    static final RegistryBuilder REGISTRY = endpoint -> { };
    static final ZLinkRegistrySpotRemoteAddressesOptions REGISTRY_SPOT_REMOTE_ADDRESSES =
        routerChannelId -> { };
    static final ClientServerChannelBuilder CLIENT_SERVER_CHANNEL = new NoopClientServerChannelBuilder();
    static final FanoutChannelBuilder FANOUT_CHANNEL = new NoopFanoutChannelBuilder();
    static final DealerMeshChannelBuilder DEALER_MESH_CHANNEL = new NoopDealerMeshChannelBuilder();
    static final RouteMeshChannelBuilder ROUTE_MESH_CHANNEL = new NoopRouteMeshChannelBuilder();
    static final ZLinkSpotMeshBuilder SPOT_MESH = new NoopSpotMeshBuilder();
    static final ZLinkStreamNodeBuilder STREAM_NODE = new NoopStreamNodeBuilder();
    static final ZLinkDispatchOptions DISPATCH = new NoopDispatchOptions();

    private NoopBuilders() {
    }

    private static final class NoopClientServerChannelBuilder implements ClientServerChannelBuilder {
        @Override public void enableServer() { }
        @Override public void enableServer(Consumer<ChannelServerCapabilityBuilder> configure) {
            configure.accept(endpoint -> { });
        }
        @Override public void enableClient() { }
        @Override public void enableClient(Consumer<ClientCapabilityBuilder> configure) {
            configure.accept(clientConfigure -> clientConfigure.accept(endpoint -> { }));
        }
        @Override public void addHandlerGroup(String groupName) { }
        @Override public <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType, Class<TMessage> messageType, String packetName) { }
        @Override public <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
        void addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType,
            String packetName) { }
        @Override public void enableSpotRouteEgress(String targetSpotNodeChannelName) { }
    }

    private static final class NoopFanoutChannelBuilder implements FanoutChannelBuilder {
        @Override public void enablePublisher() { }
        @Override public void enablePublisher(Consumer<ChannelPublisherCapabilityBuilder> configure) {
            configure.accept(endpoint -> { });
        }
        @Override public void enableSubscriber() { }
        @Override public void enableSubscriber(Consumer<SubscriberCapabilityBuilder> configure) {
            configure.accept(clientConfigure -> clientConfigure.accept(endpoint -> { }));
        }
        @Override public void addHandlerGroup(String groupName) { }
        @Override public <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
            Class<THandler> handlerType, Class<TMessage> messageType, String packetName) { }
        @Override public void addPublishHandler(Class<?> handlerType, String packetName) { }
    }

    private static final class NoopDealerMeshChannelBuilder implements DealerMeshChannelBuilder {
        @Override public void enableClient() { }
        @Override public void enableClient(Consumer<ClientCapabilityBuilder> configure) {
            configure.accept(clientConfigure -> clientConfigure.accept(endpoint -> { }));
        }
        @Override public void addHandlerGroup(String groupName) { }
    }

    private static final class NoopRouteMeshChannelBuilder implements RouteMeshChannelBuilder {
        @Override public void bind(String endpoint) { }
        @Override public void configureRouting(Consumer<ZLinkRouteConfigBuilder> configure) {
            configure.accept(routingId -> { });
        }
        @Override public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(endpoint -> { });
        }
        @Override public void addHandlerGroup(String groupName) { }
        @Override public <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
            Class<THandler> handlerType, Class<TMessage> messageType, String packetName) { }
        @Override public <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
        void addRequestHandler(
            Class<THandler> handlerType,
            Class<TRequest> requestType,
            Class<TReply> replyType,
            String packetName) { }
        @Override public void enableSpotRouteEgress(String targetSpotNodeChannelName) { }
    }

    private static final class NoopSpotMeshBuilder implements ZLinkSpotMeshBuilder {
        @Override public void useDiscovery(Consumer<RegistryBuilder> configure) {
            configure.accept(REGISTRY);
        }
        @Override public void addNode(String spotNodeName, Consumer<ZLinkSpotNodeBuilder> configure) {
            configure.accept(new NoopSpotNodeBuilder());
        }
    }

    private static final class NoopSpotNodeBuilder implements ZLinkSpotNodeBuilder {
        @Override public void enableRouter() { }
        @Override public void enableRouter(Consumer<SpotRouterCapabilityBuilder> configure) {
            configure.accept(new NoopSpotRouterCapabilityBuilder());
        }
        @Override public void enablePubSub() { }
        @Override public void enablePubSub(Consumer<SpotPubSubCapabilityBuilder> configure) {
            configure.accept(new NoopSpotPubSubCapabilityBuilder());
        }
        @Override public void addSpotFactory(Class<? extends ZLinkSpot> spotType) { }
        @Override public void addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType) { }
    }

    private static final class NoopSpotRouterCapabilityBuilder implements SpotRouterCapabilityBuilder {
        @Override public void setRouterBind(String endpoint) { }
        @Override public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(endpoint -> { });
        }
    }

    private static final class NoopSpotPubSubCapabilityBuilder implements SpotPubSubCapabilityBuilder {
        @Override public void setPubBind(String endpoint) { }
        @Override public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(endpoint -> { });
        }
    }

    private static final class NoopStreamNodeBuilder implements ZLinkStreamNodeBuilder {
        @Override public void bind(String endpoint) { }
        @Override public void attachActorGateway(String spotNodeName) { }
        @Override public void registerSession(Class<? extends ZLinkSession> sessionType) { }
    }

    private static final class NoopDispatchOptions implements ZLinkDispatchOptions {
        @Override public ZLinkDispatchMode spotDispatchMode() { return ZLinkDispatchMode.COMPILED; }
        @Override public void setSpotDispatchMode(ZLinkDispatchMode mode) { }
        @Override public ZLinkDispatchMode streamDispatchMode() { return ZLinkDispatchMode.COMPILED; }
        @Override public void setStreamDispatchMode(ZLinkDispatchMode mode) { }
        @Override public ZLinkUnhandledDispatchOptions unhandled() {
            return new NoopUnhandledDispatchOptions();
        }
        @Override public ZLinkDiagnosticsOptions diagnostics() {
            return new NoopDiagnosticsOptions();
        }
    }

    private static final class NoopUnhandledDispatchOptions implements ZLinkUnhandledDispatchOptions {
        @Override public void setRequest(ZLinkUnhandledDispatchAction action) { }
        @Override public void setSend(ZLinkUnhandledDispatchAction action) { }
        @Override public void setPublish(ZLinkUnhandledDispatchAction action) { }
        @Override public void setSendLogLevel(ZLinkLogLevel level) { }
        @Override public void setPublishLogLevel(ZLinkLogLevel level) { }
    }

    private static final class NoopDiagnosticsOptions implements ZLinkDiagnosticsOptions {
        @Override public void setMessageFlow(ZLinkMessageFlowLogMode mode) { }
        @Override public void setSampleRate(double sampleRate) { }
        @Override public void setIncludeMessageSizes(boolean enabled) { }
        @Override public void setIncludeNativeDiagnostics(boolean enabled) { }
    }
}
