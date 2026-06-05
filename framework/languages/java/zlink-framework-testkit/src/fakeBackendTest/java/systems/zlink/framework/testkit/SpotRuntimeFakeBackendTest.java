package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletionException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkSpotActorLeft;
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRegistrySpotRemoteAddressResolver;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;

final class SpotRuntimeFakeBackendTest {
    @Test
    void spotManagerCreateListFindAndRemoveUseBackendSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> router.bindRouter("inproc://spot-router"));
                node.enablePubSub(pubsub -> pubsub.bindPubSub("inproc://spot-pub"));
                node.addSpotFactory(GameSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId rid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            assertEquals(true, runtime.spotManager()
                .createAsync(GameSpot.class, rid)
                .toCompletableFuture()
                .join()
                .created());
            assertEquals(false, runtime.spotManager()
                .getOrCreateAsync(GameSpot.class, rid)
                .toCompletableFuture()
                .join()
                .created());
            assertEquals(Optional.of(rid), runtime.spotManager()
                .findAsync(rid)
                .toCompletableFuture()
                .join()
                .map(info -> info.spotRid()));
            assertEquals(List.of(rid), runtime.spotManager()
                .listAsync()
                .toCompletableFuture()
                .join()
                .stream()
                .map(info -> info.spotRid())
                .toList());
            assertEquals(true, runtime.spotManager()
                .removeAsync(rid)
                .toCompletableFuture()
                .join());
            assertEquals(false, runtime.spotManager()
                .removeAsync(rid)
                .toCompletableFuture()
                .join());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.setPubBind.inproc://spot-pub",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "close.spot.1",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotManagerCreatePayloadIsPassedToOnCreate() {
        PayloadSpot.lastCreatePayload.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> router.bindRouter("inproc://payload-router"));
                node.addSpotFactory(PayloadSpot.class);
            }));
        RoutingId rid = RoutingId.from("payload-spot");

        try (Message first = Message.from("first".getBytes(StandardCharsets.UTF_8));
             Message second = Message.from("second".getBytes(StandardCharsets.UTF_8));
             ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new FakeZLinkBackendAdapterFactory())) {
            assertEquals(true, runtime.spotManager()
                .getOrCreateAsync(PayloadSpot.class, rid, List.of(first))
                .toCompletableFuture()
                .join()
                .created());
            assertEquals("first", PayloadSpot.lastCreatePayload.get());

            assertEquals(false, runtime.spotManager()
                .getOrCreateAsync(PayloadSpot.class, rid, List.of(second))
                .toCompletableFuture()
                .join()
                .created());
            assertEquals("first", PayloadSpot.lastCreatePayload.get());
        }
    }

    @Test
    void spotOutboundChannelCallsUseBackendSpot() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(OutboundSpot.class)));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToChannel("play-events", "hello")
                .packetName("Greeting")
                .submitAsync()
                .toCompletableFuture()
                .join();
            OutboundSpot.context.outbound()
                .requestToChannel("play-rpc", "ping")
                .packetName("Ping")
                .submitAsync(String.class);
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "spot.1.sendToChannel.play-events.Greeting",
                "spot.1.requestToChannel.play-rpc.Ping",
                "close.context",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotContextDispatchesRegisteredPacketRequestAndSubscriptionHandlers() {
        HandlerSpot.dispatches.clear();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enablePubSub();
                node.addSpotFactory(HandlerSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(HandlerSpot.class, RoutingId.from("handler-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotRoute("String", "hello");
            backendFactory.dispatchSpotRequest("SpotQuery", "ping", 7);
            backendFactory.dispatchSpotSubscription("stage.events", "String", "opened");
        }

        assertEquals(
            List.of(
                "packet:hello",
                "request:ping",
                "subscription:opened"),
            HandlerSpot.dispatches);
        assertEquals(List.of("reply:ping"), backendFactory.spotReplies());
        assertTrue(backendFactory.calls().contains("spot.1.setSubscription.stage.events"));
    }

    @Test
    void spotOutboundSpotCallsUseBackendSpotRouteOperations() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(OutboundSpot.class)));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Greeting")
                .submitAsync()
                .toCompletableFuture()
                .join();
            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(RoutingId.from("target-spot"), "ping")
                    .packetName("Ping")
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "spot.1.sendToSpot.spot-node.target-spot.Greeting",
                "spot.1.requestToSpot.spot-node.target-spot.Ping",
                "close.context",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotOutboundUsesMessageTypePacketNameByDefault() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(OutboundSpot.class)));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), new SpotGreeting("hello"))
                .submitAsync()
                .toCompletableFuture()
                .join();
            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(RoutingId.from("target-spot"), new SpotQuestion("ping"))
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "spot.1.sendToSpot.spot-node.target-spot.SpotGreeting",
                "spot.1.requestToSpot.spot-node.target-spot.SpotQuestion",
                "close.context",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void registrySpotRemoteAddressResolverReturnsRouteModelFromSpotDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("inproc://registry"));
        options.addRouteMeshChannel("play", route -> route.bind("inproc://play"));
        options.useRegistrySpotRemoteAddresses("game");
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play-node", node -> {
                node.enableRouter();
                node.addSpotFactory(GameSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            ZLinkRegistrySpotRemoteAddressResolver resolver =
                new ZLinkRegistrySpotRemoteAddressResolver(
                    runtime,
                    options.registration());

            var route = resolver.resolveSpotRemoteAddressAsync(RoutingId.from("room-1"))
                .toCompletableFuture()
                .join();

            assertEquals("play", route.routerChannelId());
            assertEquals(RoutingId.from("node"), route.targetNodeRid());
            assertEquals(RoutingId.from("room-1"), route.spotRid());
            assertEquals(ZLinkSpotKind.USER, route.spotKind());
        }

        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("spotNode.attachDiscovery.")));
    }

    @Test
    void spotOutboundUsesConfiguredClientServerEgressChannel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("egress", channel -> {
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://ingress-server")));
            channel.enableSpotRouteEgress("ingress");
        });
        options.addClientServerChannel("ingress", channel -> {
            channel.enableServer(server -> server.bind("inproc://ingress-server"));
            channel.addSendHandler(NoopSendHandler.class, String.class, "Noop");
        });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("ingress", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://ingress-router")));
                node.addSpotFactory(OutboundSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Greeting")
                .submitAsync()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains(
            "dealer.send.__zlink.routed_spot.egress.send"));
    }

    @Test
    void spotOutboundUsesConfiguredRouteMeshEgressChannel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMeshChannel("egress", route -> {
            route.bind("inproc://egress-route");
            route.useManualConnections(endpoints ->
                endpoints.connect("inproc://egress-peer"));
            route.enableSpotRouteEgress("ingress");
        });
        options.addRouteMeshChannel("ingress", route -> {
            route.configureRouting(routing ->
                routing.setRoutingId(RoutingId.from("ingress-route")));
            route.bind("inproc://ingress-route");
            route.useManualConnections(endpoints ->
                endpoints.connect("inproc://ingress-peer"));
        });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("ingress", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://ingress-router")));
                node.addSpotFactory(OutboundSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(RoutingId.from("target-spot"), "ping")
                    .packetName("Ping")
                    .submitAsync(String.class)
                    .toCompletableFuture()
                    .join());
        }

        assertTrue(backendFactory.calls().contains(
            "router.request.ingress-route.__zlink.routed_spot.egress.request"));
    }

    @Test
    void routeMeshSpotEgressRequiresTargetRoutePeerRoutingId() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMeshChannel("egress", route -> {
            route.bind("inproc://egress-route");
            route.useManualConnections(endpoints ->
                endpoints.connect("inproc://egress-peer"));
            route.enableSpotRouteEgress("ingress");
        });
        options.addRouteMeshChannel("ingress", route -> {
            route.bind("inproc://ingress-route");
            route.useManualConnections(endpoints ->
                endpoints.connect("inproc://ingress-peer"));
        });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("ingress", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://ingress-router")));
                node.addSpotFactory(OutboundSpot.class);
            }));

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new FakeZLinkBackendAdapterFactory())) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            assertThrows(
                CompletionException.class,
                () -> OutboundSpot.context.outbound()
                    .sendToSpot(RoutingId.from("target-spot"), "hello")
                    .packetName("Ping")
                    .submitAsync()
                    .toCompletableFuture()
                    .join());
        }
    }

    @Test
    void routeMeshSpotEgressUsesRegistryQueryRoutingId() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addRouteMeshChannel("egress", route -> {
            route.bind("inproc://egress-route");
            route.enableSpotRouteEgress("ingress");
        });
        options.addRouteMeshChannel("ingress", route ->
            route.bind("inproc://ingress-route"));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("ingress");
                node.addSpotFactory(OutboundSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Ping")
                .submitAsync()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains(
            "registryQueryClient.connect.tcp://127.0.0.1:17001"));
        assertTrue(backendFactory.calls().contains(
            "router.send.registry-route-peer.__zlink.routed_spot.egress.send"));
    }

    @Test
    void routeMeshSpotEgressUsesDiscoveryMemberPeerRoutingIdBeforeRegistryQuery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addRouteMeshChannel("egress-discovery", route -> {
            route.bind("inproc://egress-discovery");
            route.enableSpotRouteEgress("ingress-discovery");
        });
        options.addRouteMeshChannel("ingress-discovery", route ->
            route.bind("inproc://ingress-discovery"));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("ingress-discovery");
                node.addSpotFactory(OutboundSpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Ping")
                .submitAsync()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains(
            "router.send.discovery-route-peer.__zlink.routed_spot.egress.send"));
    }

    @Test
    void spotOutboundRejectsAmbiguousEgressChannels() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("egress-a", channel -> {
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://ingress-a")));
            channel.enableSpotRouteEgress("ingress");
        });
        options.addClientServerChannel("egress-b", channel -> {
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://ingress-b")));
            channel.enableSpotRouteEgress("ingress");
        });
        options.addClientServerChannel("ingress", channel -> {
            channel.enableServer(server -> server.bind("inproc://ingress-server"));
            channel.addSendHandler(NoopSendHandler.class, String.class, "Noop");
        });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("ingress", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://ingress-router")));
                node.addSpotFactory(OutboundSpot.class);
            }));

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, new FakeZLinkBackendAdapterFactory())) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            assertThrows(
                ZLinkConfigurationException.class,
                () -> OutboundSpot.context.outbound()
                    .sendToSpot(RoutingId.from("target-spot"), "hello"));
        }
    }

    @Test
    void ambientSpotOutboundWorksInsideSpotCallback() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(AmbientOutboundSpot.class)));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            AmbientOutboundSpot.outbound = runtime.spotOutbound();
            runtime.spotManager()
                .createAsync(AmbientOutboundSpot.class, RoutingId.from("game-3"))
                .toCompletableFuture()
                .join();
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spot.1.sendToChannel.play-events.Greeting"));
    }

    @Test
    void spotPublisherClientPublishesThroughAttachedPublisherSpot() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("publisher", node -> {
                node.enablePubSub();
                node.attachSpotPublisherClient("game.stage", publisher ->
                    publisher.useManualConnections(endpoints ->
                        endpoints.connect("inproc://game-stage-pub")));
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotPublisherClient()
                .publishSpot("game.stage", "stage.events", "opened")
                .packetName("StageOpened")
                .submitAsync()
                .toCompletableFuture()
                .join();
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.connectPeer.inproc://game-stage-pub",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.publish.stage.events.StageOpened",
                "close.context",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotRouterAndPubSubManualPeersConnectThroughBackendNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRoutingId(RoutingId.from("spot-node-1"));
                    router.bindRouter("inproc://spot-router");
                    router.useManualConnections(endpoints ->
                        endpoints.connect("inproc://spot-router-peer"));
                });
                node.enablePubSub(pubsub -> {
                    pubsub.bindPubSub("inproc://spot-pub");
                    pubsub.useManualConnections(endpoints ->
                        endpoints.connect("inproc://spot-pub-peer"));
                });
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.setRoutingId",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.setPubBind.inproc://spot-pub",
                "spotNode.connectPeer.inproc://spot-router-peer",
                "spotNode.connectPeer.inproc://spot-pub-peer",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void attachedSpotChannelClientManualConnectionAttachesDealerToBackendNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node ->
                node.attachChannelClient("profile", client ->
                    client.useManualConnections(endpoints ->
                        endpoints.connect("inproc://profile-server")))));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.connect.inproc://profile-server",
                "spotNode.attachChannelDealerManual.profile",
                "close.context",
                "close.dealer",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void attachedSpotChannelClientDiscoveryAttachesDealerToBackendNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.attachChannelClient("profile")));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "create.discovery.game",
                "discovery.game.connectRegistry.tcp://127.0.0.1:17001",
                "spotNode.attachDiscovery.discovery.game",
                "create.dealer",
                "dealer.setChannelName.profile",
                "create.discovery.profile",
                "discovery.profile.connectRegistry.tcp://127.0.0.1:17001",
                "dealer.attachDiscovery.discovery.profile",
                "spotNode.attachChannelDealer",
                "close.context",
                "close.dealer",
                "close.discovery.game",
                "close.discovery.profile",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void acceptedSpotRouteChannelManualConnectionsAttachRouterChannelPeers() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMeshChannel("api", route -> {
            route.bind("inproc://api-route");
            route.useManualConnections(endpoints ->
                endpoints.connect("inproc://api-route-peer"));
        });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.bindRouter("inproc://spot-router"));
                node.acceptSpotRoutesFromChannel("api", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://api-router-peer")));
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.router",
                "router.setChannelName.api",
                "router.connect.inproc://api-route-peer",
                "router.bind.inproc://api-route",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.connectRouterChannelPeer.api.inproc://api-router-peer",
                "close.router",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void acceptedSpotRouteChannelDiscoveryAttachesRouteDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addClientServerChannel("api", channel -> { });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.bindRouter("inproc://spot-router"));
                node.acceptSpotRoutesFromChannel("api");
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.discovery.api",
                "discovery.api.connectRegistry.tcp://127.0.0.1:17001",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://spot-router",
                "create.discovery.game",
                "discovery.game.connectRegistry.tcp://127.0.0.1:17001",
                "spotNode.attachDiscovery.discovery.game",
                "create.discovery.api",
                "discovery.api.connectRegistry.tcp://127.0.0.1:17001",
                "spotNode.attachSpotRouteChannelDiscovery.api.discovery.api",
                "close.discovery.api",
                "close.context",
                "close.discovery.game",
                "close.discovery.api",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void entrySpotRoutingIdAppliesToBackendEntrySpotBeforeBind() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.enableRouter(router ->
                    router.bindRouter("inproc://spot-router"));
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.entrySpot",
                "create.entrySpot",
                "entrySpot.setRoutingId",
                "spotNode.setRouterBind.inproc://spot-router",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void registeredEntrySpotIsActivatedAndClosedWithBackendEntrySpot() {
        LifecycleEntrySpot.configureCount.set(0);
        LifecycleEntrySpot.initializeCount.set(0);
        LifecycleEntrySpot.closingCount.set(0);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.enableRouter(router ->
                    router.bindRouter("inproc://spot-router"));
                node.addEntrySpot(LifecycleEntrySpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            assertEquals(1, LifecycleEntrySpot.configureCount.get());
            assertEquals(1, LifecycleEntrySpot.initializeCount.get());
        }

        assertEquals(1, LifecycleEntrySpot.closingCount.get());
        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.entrySpot",
                "create.entrySpot",
                "entrySpot.setRoutingId",
                "entrySpot.onDispatchEvent",
                "spotNode.setRouterBind.inproc://spot-router",
                "close.context",
                "close.entrySpot",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void entrySpotDispatchReadableDrainsUnhandledActorJoinWithRejectReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
            }));
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable("player-1");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorJoin.DONT_WAIT"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.1"));
    }

    @Test
    void entrySpotActorJoinReadableInvokesRegisteredHandlerAndRepliesOk() {
        ActorJoinHandler.lastJoin.set(null);
        ActorJoinedHandler.lastJoin.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorJoin.DONT_WAIT"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.0"));
        assertEquals("player-1:join-request", ActorJoinHandler.lastJoin.get());
        assertEquals("player-1:JOIN_ENTRY_SPOT:true", ActorJoinedHandler.lastJoin.get());
    }

    @Test
    void entrySpotActorReadableInvokesRegisteredActorSendHandlerOnActorQueue() {
        ActorSendHandler.lastMessage.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            backendFactory.dispatchEntrySpotActorMessage(
                "player-1",
                "String",
                "mark-x");
        }

        assertEquals("player-1:mark-x", ActorSendHandler.lastMessage.get());
    }

    @Test
    void entrySpotActorReadableInvokesRegisteredActorRequestHandlerAndRepliesBoundSession() {
        ActorRequestHandler.lastRequest.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            backendFactory.dispatchEntrySpotActorStreamRequest(
                "player-1",
                "ActorRequestString",
                "7",
                42);
            awaitCall(backendFactory, "spotNode.sendActorBoundSession.player-1.");
        }

        assertEquals("player-1:7", ActorRequestHandler.lastRequest.get());
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("spotNode.sendActorBoundSession.player-1.")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void spotContextLeaveActorMarksActorLeftAndInvokesRegisteredHandler() {
        ActorLeftHandler.lastLeave.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
                node.addSpotFactory(OutboundSpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            ZLinkActor actor = runtime.actorManager()
                .findAsync("player-1")
                .toCompletableFuture()
                .join()
                .orElseThrow();

            OutboundSpot.context.leaveActorAsync(actor).toCompletableFuture().join();
        }

        assertEquals("player-1:LEAVE_SPOT:false", ActorLeftHandler.lastLeave.get());
    }

    @Test
    void entrySpotActorLifecycleReadableDrainsLeftEventAndInvokesRegisteredHandler() {
        ActorLeftHandler.lastLeave.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            backendFactory.dispatchEntrySpotActorLifecycleLeft("player-1");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorLifecycle.DONT_WAIT"));
        assertEquals("player-1:LEAVE_SPOT:false", ActorLeftHandler.lastLeave.get());
    }

    @Test
    void entrySpotActorLifecycleJoinedBindsActorContextToUserSpot() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.configureEntrySpot(entry ->
                    entry.setRoutingId(RoutingId.from("entry-spot")));
                node.addEntrySpot(LifecycleEntrySpot.class);
                node.addSpotFactory(OutboundSpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId spotRid = RoutingId.from("game-joined");

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.spotManager()
                .createAsync(OutboundSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            backendFactory.dispatchEntrySpotActorLifecycleJoined("player-1", spotRid);

            assertEquals(Optional.of(spotRid), actor.context().spotRid());
            assertSame(OutboundSpot.instance, actor.context().getSpot(OutboundSpot.class));
        }
    }

    @Test
    void spotPublisherClientRejectsUnattachedChannel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("publisher", node -> {
                node.enablePubSub();
                node.attachSpotPublisherClient("game.stage");
            }));

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(
                     options,
                     new FakeZLinkBackendAdapterFactory())) {
            assertThrows(
                ZLinkConfigurationException.class,
                () -> runtime.spotPublisherClient()
                    .publishSpot("missing", "stage.events", "opened"));
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onInitializeAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class PayloadSpot implements ZLinkSpot {
        static final AtomicReference<String> lastCreatePayload = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onCreateAsync(List<Message> createParts) {
            lastCreatePayload.set(createParts.isEmpty()
                ? ""
                : new String(createParts.getFirst().toByteArray(), StandardCharsets.UTF_8));
            return CompletableFuture.completedFuture(null);
        }
    }

    private static void awaitCall(
        FakeZLinkBackendAdapterFactory backendFactory,
        String prefix) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(1).toNanos();
        while (System.nanoTime() < deadline) {
            if (backendFactory.calls().stream().anyMatch(call -> call.startsWith(prefix))) {
                return;
            }
            Thread.onSpinWait();
        }
    }

    public static final class OutboundSpot implements ZLinkSpot {
        static OutboundSpot instance;
        static ZLinkSpotContext context;

        public OutboundSpot(ZLinkSpotContext context) {
            OutboundSpot.instance = this;
            OutboundSpot.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }
    }

    public static final class HandlerSpot implements ZLinkSpot {
        static final List<String> dispatches = new java.util.ArrayList<>();
        private final ZLinkSpotContext context;

        public HandlerSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addPacket(SpotCommandHandler.class);
            context.handlers().addPacket(SpotQueryHandler.class);
            context.handlers().addSubscribe("stage.events", SpotEventHandler.class);
        }
    }

    public static final class SpotCommandHandler
        implements ZLinkSpotPacketHandler<HandlerSpot, String> {
        @Override
        public CompletionStage<Void> handleAsync(HandlerSpot spot, String message) {
            HandlerSpot.dispatches.add("packet:" + message);
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class SpotQueryHandler {
        @ZLinkSpotRequest(packetName = "SpotQuery")
        public CompletionStage<String> handleAsync(HandlerSpot spot, String request) {
            HandlerSpot.dispatches.add("request:" + request);
            return CompletableFuture.completedFuture("reply:" + request);
        }
    }

    public static final class SpotEventHandler
        implements ZLinkSpotSubscriptionHandler<HandlerSpot, String> {
        @Override
        public CompletionStage<Void> handleAsync(HandlerSpot spot, String message) {
            HandlerSpot.dispatches.add("subscription:" + message);
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class NoopSendHandler
        implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(
            String message,
            ZLinkSendContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class AmbientOutboundSpot implements ZLinkSpot {
        static ZLinkSpotOutbound outbound;

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onCreateAsync(List<Message> createParts) {
            return outbound
                .sendToChannel("play-events", "hello")
                .packetName("Greeting")
                .submitAsync();
        }
    }

    public static final class LifecycleEntrySpot implements ZLinkEntrySpot {
        static final AtomicInteger configureCount = new AtomicInteger();
        static final AtomicInteger initializeCount = new AtomicInteger();
        static final AtomicInteger closingCount = new AtomicInteger();
        private final ZLinkEntrySpotContext context;

        public LifecycleEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            configureCount.incrementAndGet();
        }

        @Override
        public CompletionStage<Void> onInitializeAsync() {
            initializeCount.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onClosingAsync() {
            closingCount.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorJoinHandler {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();

        @ZLinkSpotActorJoin
        public CompletionStage<String> join(PlayerActor actor, String request) {
            lastJoin.set(actor.actorId() + ":" + request);
            return CompletableFuture.completedFuture("joined:" + request);
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorSendHandler {
        static final AtomicReference<String> lastMessage = new AtomicReference<>();

        @ZLinkSpotActorSend
        public CompletionStage<Void> send(PlayerActor actor, String message) {
            lastMessage.set(actor.actorId() + ":" + message);
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorJoinedHandler {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();

        @ZLinkSpotPostActorJoined
        public CompletionStage<Void> joined(
            PlayerActor actor,
            ZLinkSpotActorChangeResult result) {
            lastJoin.set(actor.actorId()
                + ":"
                + result.kind()
                + ":"
                + actor.context().isJoined());
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorLeftHandler {
        static final AtomicReference<String> lastLeave = new AtomicReference<>();

        @ZLinkSpotActorLeft
        public CompletionStage<Void> left(
            PlayerActor actor,
            ZLinkSpotActorChangeResult result) {
            lastLeave.set(actor.actorId()
                + ":"
                + result.kind()
                + ":"
                + actor.context().isJoined());
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorRequestHandler {
        static final AtomicReference<String> lastRequest = new AtomicReference<>();

        @ZLinkSpotActorRequest(packetName = "ActorRequestString")
        public CompletionStage<String> request(PlayerActor actor, String request) {
            lastRequest.set(actor.actorId() + ":" + request);
            return CompletableFuture.completedFuture("reply:" + request);
        }
    }

    public static final class PlayerActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;

        PlayerActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }
    }

    public static final class PlayerActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> createAsync(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public record SpotGreeting(String value) {
    }

    @ZLinkPacket("SpotQuestion")
    public record SpotQuestion(String value) {
    }
}
