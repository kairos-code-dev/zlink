package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class DefaultZLinkFrameworkOptionsTest {
    @Test
    void addClientServerChannelRejectsDuplicateChannelName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> { });

        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addFanoutChannel("profile", channel -> { }));
    }

    @Test
    void setDefaultTimeoutRejectsZero() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.setDefaultTimeout(Duration.ZERO));
    }

    @Test
    void clientServerChannelClientWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> channel.enableClient());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelClientWithManualConnectionIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://profile-server"))));

        options.validate();
    }

    @Test
    void clientServerChannelClientCannotMixDiscoveryAndManualConnections() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(registry -> registry.add("tcp://127.0.0.1:17001"));
        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://profile-server"))));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> channel.enableServer());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithoutRequestHandlerIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel ->
            channel.enableServer(server -> server.bind("inproc://profile-server")));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelRejectsBlankRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () -> options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, " ");
        }));
    }

    @Test
    void clientServerChannelServerWithBindIsAccepted() {
        DefaultZLinkFrameworkOptions accepted = new DefaultZLinkFrameworkOptions();
        accepted.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        accepted.validate();
    }

    @Test
    void fanoutChannelPublisherWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addFanoutChannel("events", channel -> channel.enablePublisher());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelSubscriberWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber();
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelSubscriberCannotMixDiscoveryAndManualConnections() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(registry -> registry.add("tcp://127.0.0.1:17001"));
        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect("inproc://events")));
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelRejectsDuplicatePublishHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect("inproc://events")));
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel ->
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route")));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel -> channel.bind("inproc://route"));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelCannotMixDiscoveryAndManualConnections() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(registry -> registry.add("tcp://127.0.0.1:17001"));
        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route-peer"));
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route-peer"));
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void streamNodeRejectsMultipleSessionTypes() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            options.addStreamNode("gateway", stream -> {
                stream.bind("inproc://gateway");
                stream.registerSession(GameSession.class);
                stream.registerSession(GameSession.class);
            }));
    }

    @Test
    void streamNodeActorGatewayRequiresConfiguredSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void streamNodeWithActorGatewayAndSpotNodeIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(TestSpot.class)));
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        options.validate();
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class EventHandler implements ZLinkPublishHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkPublishContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class RouteEchoHandler
        implements systems.zlink.framework.channels.ZLinkRouteRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(
            String request,
            systems.zlink.framework.channels.ZLinkRouteRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class TestSpot implements systems.zlink.framework.spots.ZLinkSpot {
        @Override
        public systems.zlink.framework.spots.ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
