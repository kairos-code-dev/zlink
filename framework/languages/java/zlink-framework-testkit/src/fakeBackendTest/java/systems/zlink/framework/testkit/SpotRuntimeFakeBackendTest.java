package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRegistrySpotRemoteAddressResolver;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;

final class SpotRuntimeFakeBackendTest {
    @Test
    void spotManagerCreateListFindAndCloseUseBackendSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://spot-router");
                node.enablePubSub("inproc://spot-pub");
                node.addSpotFactory(GameSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId rid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            assertEquals(ZLinkSpotCreateState.CREATED, runtime.spotManager()
                .create(GameSpot.class, rid)
                .toCompletableFuture()
                .join()
                .state());
            assertEquals(ZLinkSpotCreateState.EXISTING, runtime.spotManager()
                .getOrCreate(GameSpot.class, rid)
                .toCompletableFuture()
                .join()
                .state());
            assertEquals(Optional.of(rid), runtime.spotManager()
                .find(rid)
                .toCompletableFuture()
                .join()
                .map(info -> info.spotRid()));
            assertEquals(List.of(rid), runtime.spotManager()
                .list()
                .toCompletableFuture()
                .join()
                .stream()
                .map(info -> info.spotRid())
                .toList());
            assertEquals(true, runtime.spotManager()
                .close(rid)
                .toCompletableFuture()
                .join());
            assertEquals(false, runtime.spotManager()
                .close(rid)
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://payload-router");
                node.addSpotFactory(PayloadSpot.class); }; };
        RoutingId rid = RoutingId.from("payload-spot");

        try (Message first = Message.from("first".getBytes(StandardCharsets.UTF_8));
             Message second = Message.from("second".getBytes(StandardCharsets.UTF_8));
             ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new FakeZLinkBackendAdapterFactory())) {
            assertEquals(ZLinkSpotCreateState.CREATED, runtime.spotManager()
                .getOrCreate(PayloadSpot.class, rid, first)
                .toCompletableFuture()
                .join()
                .state());
            assertEquals("first", PayloadSpot.lastCreatePayload.get());

            assertEquals(ZLinkSpotCreateState.EXISTING, runtime.spotManager()
                .getOrCreate(PayloadSpot.class, rid, second)
                .toCompletableFuture()
                .join()
                .state());
            assertEquals("first", PayloadSpot.lastCreatePayload.get());
        }
    }

    @Test
    void spotOutboundChannelCallsUseBackendSpot() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToChannel("play-events", "hello")
                .packetName("Greeting")
                .submit()
                .toCompletableFuture()
                .join();
            OutboundSpot.context.outbound()
                .requestToChannel("play-rpc", "ping")
                .packetName("Ping")
                .submit(String.class);
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enablePubSub("inproc://spot-pub");
                node.addSpotFactory(HandlerSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(HandlerSpot.class, RoutingId.from("handler-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotRoute("String", "hello");
            backendFactory.dispatchSpotRequest("SpotQuery", "ping", 7);
            backendFactory.dispatchSpotSubscription("stage.events", "String", "opened");
            awaitCondition(() -> HandlerSpot.dispatches.size() == 3);
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
    void userSpotDispatchesRouteHandlersOnSingleSpotSerialQueue() throws Exception {
        SerialSpotHandler.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(SerialSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(SerialSpot.class, RoutingId.from("serial-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotRoute("String", "first");
            SerialSpotHandler.firstStarted.get(1, TimeUnit.SECONDS);
            backendFactory.dispatchSpotRoute("String", "second");

            assertFalse(
                SerialSpotHandler.secondStarted.isDone(),
                SerialSpotHandler.events.toString());

            SerialSpotHandler.releaseFirst.complete(null);
            SerialSpotHandler.secondStarted.get(1, TimeUnit.SECONDS);
        }

        assertEquals(
            List.of("start:first", "end:first", "start:second", "end:second"),
            SerialSpotHandler.events);
    }

    @Test
    void userSpotActorJoinWaitsOnSingleSpotSerialQueue() throws Exception {
        SerialSpotHandler.reset();
        SerialActorJoinHandler.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addActorFactory("player", PlayerActorFactory.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(SerialSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        ZLinkHandlerFactory reflection = ZLinkHandlerFactory.reflection();
        ZLinkHandlerFactory handlerFactory = handlerType -> {
            if (handlerType == SerialActorJoinHandler.class) {
                return new SerialActorJoinHandler();
            }
            return reflection.create(handlerType);
        };

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.newFrameworkRuntime(
                 options,
                 backendFactory,
                 new SerialJoinSerializer(),
                 handlerFactory)) {
            runtime.spotManager()
                .create(SerialSpot.class, RoutingId.from("serial-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotActorJoinReadable(
                "player-serial",
                null,
                "join");
            awaitFuture(SerialActorJoinHandler.started, backendFactory.calls());
            backendFactory.dispatchSpotRoute("String", "second");

            assertFalse(
                SerialSpotHandler.secondStarted.isDone(),
                SerialActorJoinHandler.events.toString() + SerialSpotHandler.events);

            SerialActorJoinHandler.release.complete(null);
            SerialSpotHandler.secondStarted.get(1, TimeUnit.SECONDS);
        }

        assertEquals(
            List.of("join:start:player-serial:join", "join:end:player-serial"),
            SerialActorJoinHandler.events);
        assertEquals(List.of("start:second", "end:second"), SerialSpotHandler.events);
    }

    @Test
    void spotOutboundSpotCallsUseBackendSpotRouteOperations() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Greeting")
                .submit()
                .toCompletableFuture()
                .join();
            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(RoutingId.from("target-spot"), "ping")
                    .packetName("Ping")
                    .submit(String.class)
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), new SpotGreeting("hello"))
                .submit()
                .toCompletableFuture()
                .join();
            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(RoutingId.from("target-spot"), new SpotQuestion("ping"))
                    .submit(String.class)
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
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("inproc://registry"); };
        { var route = options.addRouteMeshChannel("play"); route.enableServer("inproc://play"); };
        options.useRegistrySpotRemoteAddresses("game");
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play-node"); node.enableRouter("inproc://play-router");
                node.addSpotFactory(GameSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var channel = options.addClientServerChannel("egress").enableClient("inproc://ingress-server");
            channel.enableSpotRouteEgress("ingress"); };
        { var channel = options.addClientServerChannel("ingress").enableServer("inproc://ingress-server");
            channel.addSendHandler(NoopSendHandler.class, String.class, "Noop"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.acceptSpotRoutesFromChannel("ingress", "inproc://ingress-router");
                node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Greeting")
                .submit()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains(
            "dealer.send.__zlink.routed_spot.egress.send"));
    }

    @Test
    void spotOutboundUsesConfiguredRouteMeshEgressChannel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var route = options.addRouteMeshChannel("egress"); route.enableServer("inproc://egress-route");
            route.enableClient("inproc://egress-peer");
            route.enableSpotRouteEgress("ingress"); };
        { var route = options.addRouteMeshChannel("ingress"); { var routing = route.configureRouting(); routing.setRoutingId(RoutingId.from("ingress-route")); };
            route.enableServer("inproc://ingress-route");
            route.enableClient("inproc://ingress-peer"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.acceptSpotRoutesFromChannel("ingress", "inproc://ingress-router");
                node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(RoutingId.from("target-spot"), "ping")
                    .packetName("Ping")
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());
        }

        assertTrue(backendFactory.calls().contains(
            "router.request.ingress-route.__zlink.routed_spot.egress.request"));
    }

    @Test
    void routeMeshSpotEgressRequiresTargetRoutePeerRoutingId() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var route = options.addRouteMeshChannel("egress"); route.enableServer("inproc://egress-route");
            route.enableClient("inproc://egress-peer");
            route.enableSpotRouteEgress("ingress"); };
        { var route = options.addRouteMeshChannel("ingress"); route.enableServer("inproc://ingress-route");
            route.enableClient("inproc://ingress-peer"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.acceptSpotRoutesFromChannel("ingress", "inproc://ingress-router");
                node.addSpotFactory(OutboundSpot.class); }; };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new FakeZLinkBackendAdapterFactory())) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            assertThrows(
                CompletionException.class,
                () -> OutboundSpot.context.outbound()
                    .sendToSpot(RoutingId.from("target-spot"), "hello")
                    .packetName("Ping")
                    .submit()
                    .toCompletableFuture()
                    .join());
        }
    }

    @Test
    void routeMeshSpotEgressUsesRegistryQueryRoutingId() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var route = options.addRouteMeshChannel("egress"); route.enableServer("inproc://egress-route");
            route.enableSpotRouteEgress("ingress"); };
        { var route = options.addRouteMeshChannel("ingress"); route.enableServer("inproc://ingress-route"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.acceptSpotRoutesFromChannel("ingress");
                node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Ping")
                .submit()
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
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var route = options.addRouteMeshChannel("egress-discovery"); route.enableServer("inproc://egress-discovery");
            route.enableSpotRouteEgress("ingress-discovery"); };
        { var route = options.addRouteMeshChannel("ingress-discovery"); route.enableServer("inproc://ingress-discovery"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.acceptSpotRoutesFromChannel("ingress-discovery");
                node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(RoutingId.from("target-spot"), "hello")
                .packetName("Ping")
                .submit()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains(
            "router.send.discovery-route-peer.__zlink.routed_spot.egress.send"));
    }

    @Test
    void spotOutboundRejectsAmbiguousEgressChannels() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("egress-a").enableClient("inproc://ingress-a");
            channel.enableSpotRouteEgress("ingress"); };
        { var channel = options.addClientServerChannel("egress-b").enableClient("inproc://ingress-b");
            channel.enableSpotRouteEgress("ingress"); };
        { var channel = options.addClientServerChannel("ingress").enableServer("inproc://ingress-server");
            channel.addSendHandler(NoopSendHandler.class, String.class, "Noop"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.acceptSpotRoutesFromChannel("ingress", "inproc://ingress-router");
                node.addSpotFactory(OutboundSpot.class); }; };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new FakeZLinkBackendAdapterFactory())) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(AmbientOutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            AmbientOutboundSpot.outbound = runtime.spotOutbound();
            runtime.spotManager()
                .create(AmbientOutboundSpot.class, RoutingId.from("game-3"))
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("publisher"); node.enablePubSub("inproc://spot-pub");
                node.attachSpotPublisherClient("game.stage", "inproc://game-stage-pub"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotPublisherClient()
                .publishSpot("game.stage", "stage.events", "opened")
                .packetName("StageOpened")
                .submit()
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
                "spotNode.setPubBind.inproc://spot-pub",
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://spot-router")
                    .setRouterRoutingId(RoutingId.from("spot-node-1"))
                    .connectRouter("inproc://spot-router-peer");
                node.enablePubSub("inproc://spot-pub")
                    .connectPubSub("inproc://spot-pub-peer"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.attachChannelClient("profile", "inproc://profile-server"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.attachChannelClient("profile"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var route = options.addRouteMeshChannel("api"); route.enableServer("inproc://api-route");
            route.enableClient("inproc://api-route-peer"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://spot-router");
                node.acceptSpotRoutesFromChannel("api", "inproc://api-router-peer"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var channel = options.addClientServerChannel("api");  };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://spot-router");
                node.acceptSpotRoutesFromChannel("api"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.enableRouter("inproc://spot-router"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.enableRouter("inproc://spot-router");
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            assertEquals(1, LifecycleEntrySpot.configureCount.get());
            awaitCondition(() -> LifecycleEntrySpot.initializeCount.get() == 1);
            assertEquals(1, LifecycleEntrySpot.initializeCount.get());
        }

        awaitCondition(() -> LifecycleEntrySpot.closingCount.get() == 1);
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable("player-1");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorJoin.DONT_WAIT"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.1"));
    }

    @Test
    void entrySpotActorJoinReadableCommitsAndInvokesMemberCallback() {
        LifecycleEntrySpot.lastJoin.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorJoin.DONT_WAIT"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.0"));
        assertEquals("player-1:true", LifecycleEntrySpot.lastJoin.get());
    }

    @Test
    void entrySpotActorReadableInvokesRegisteredActorSendHandlerOnActorQueue() {
        ActorSendHandler.lastMessage.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.actorManager()
                .create("player-1", "player")
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.actorManager()
                .create("player-1", "player")
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
    void entrySpotActorReadableInvokesInterfaceActorRequestHandler() {
        InterfaceEntryActorRequestHandler.lastRequest.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(InterfaceEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.newFrameworkRuntime(
                 options,
                 backendFactory,
                 new InterfaceActorSerializer(),
                 ZLinkHandlerFactory.reflection())) {
            runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            backendFactory.dispatchEntrySpotActorStreamRequest(
                "player-1",
                "InterfaceActorRequest",
                "7",
                42);
            awaitCall(backendFactory, "spotNode.sendActorBoundSession.player-1.");
        }

        assertEquals("player-1:7", InterfaceEntryActorRequestHandler.lastRequest.get());
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("spotNode.sendActorBoundSession.player-1.")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void userSpotActorJoinReadableInvokesMemberJoinAndLifecycleCallbacks() {
        InterfaceUserSpot.lastJoin.set(null);
        InterfaceUserSpot.lastPostJoin.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addActorFactory("player", PlayerActorFactory.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(InterfaceUserSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.newFrameworkRuntime(
                 options,
                 backendFactory,
                 new InterfaceActorSerializer(),
                 ZLinkHandlerFactory.reflection())) {
            runtime.spotManager()
                .create(InterfaceUserSpot.class, RoutingId.from("interface-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotActorJoinReadable(
                "player-1",
                null,
                "join-request");
            awaitCall(backendFactory, "spot.1.replyActorJoin.player-1.0");
        }

        assertEquals("player-1:join-request", InterfaceUserSpot.lastJoin.get());
        assertEquals("player-1:true", InterfaceUserSpot.lastPostJoin.get());
    }

    @Test
    void spotContextLeaveActorMarksActorLeftAndInvokesMemberCallback() {
        OutboundSpot.lastLeave.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class);
                node.addSpotFactory(OutboundSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            ZLinkActor actor = runtime.actorManager()
                .find("player-1")
                .toCompletableFuture()
                .join()
                .orElseThrow();

            OutboundSpot.context.leaveActor(actor).toCompletableFuture().join();
        }

        assertEquals("player-1:false", OutboundSpot.lastLeave.get());
    }

    @Test
    void entrySpotActorLifecycleReadableDrainsLeftEventAndInvokesMemberCallback() {
        LifecycleEntrySpot.lastLeave.set(null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            backendFactory.dispatchEntrySpotActorLifecycleLeft("player-1");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorLifecycle.DONT_WAIT"));
        assertEquals("player-1:false", LifecycleEntrySpot.lastLeave.get());
    }

    @Test
    void entrySpotActorLifecycleJoinedBindsActorContextToUserSpot() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); { var entry = node.configureEntrySpot(); entry.setRoutingId(RoutingId.from("entry-spot")); };
                node.addEntrySpot(LifecycleEntrySpot.class);
                node.addSpotFactory(OutboundSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId spotRid = RoutingId.from("game-joined");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("publisher"); node.enablePubSub("inproc://spot-pub");
                node.attachSpotPublisherClient("game.stage"); }; };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(
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
        public void onInitialize() {
                    }
    }

    public static final class PayloadSpot implements ZLinkSpot {
        static final AtomicReference<String> lastCreatePayload = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public ZLinkSpotCreateResponse onCreate(Message request) {
            lastCreatePayload.set(request.toUtf8String());
            return ZLinkSpotCreateResponse.accept();
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

    private static void awaitCondition(java.util.function.BooleanSupplier condition) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(2).toNanos();
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return;
            }
            Thread.onSpinWait();
        }
    }

    private static void awaitFuture(CompletableFuture<Void> future, List<String> calls) {
        try {
            future.get(1, TimeUnit.SECONDS);
        } catch (Exception ex) {
            throw new AssertionError(calls.toString(), ex);
        }
    }

    public static final class OutboundSpot implements ZLinkSpot {
        static OutboundSpot instance;
        static ZLinkSpotContext context;
        static final AtomicReference<String> lastLeave = new AtomicReference<>();

        public OutboundSpot(ZLinkSpotContext context) {
            OutboundSpot.instance = this;
            OutboundSpot.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void onLeaveActor(
            ZLinkActor actor,
            CancellationToken cancellationToken) {
            lastLeave.set(actor.actorId() + ":" + actor.context().isJoined());
                    }
    }

    public static final class HandlerSpot implements ZLinkSpot {
        static final List<String> dispatches = new CopyOnWriteArrayList<>();
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
        public void handle(HandlerSpot spot, String message) {
            HandlerSpot.dispatches.add("packet:" + message);
                    }
    }

    public static final class SpotQueryHandler {
        @ZLinkSpotRequest(packetName = "SpotQuery")
        public String handle(HandlerSpot spot, String request) {
            HandlerSpot.dispatches.add("request:" + request);
            return "reply:" + request;
        }
    }

    public static final class SpotEventHandler
        implements ZLinkSpotSubscriptionHandler<HandlerSpot, String> {
        @Override
        public void handle(HandlerSpot spot, String message) {
            HandlerSpot.dispatches.add("subscription:" + message);
                    }
    }

    public static final class SerialSpot implements ZLinkSpot {
        private final ZLinkSpotContext context;

        public SerialSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addPacket(SerialSpotHandler.class);
        }

        @Override
        public ZLinkSpotActorJoinResponse onActorJoin(
            ZLinkActor actor,
            Message request,
            CancellationToken cancellationToken) {
            SerialJoinRequest decoded = new SerialJoinRequest(request.toUtf8String());
            SerialActorJoinHandler.events.add(
                "join:start:" + actor.actorId() + ":" + decoded.value());
            SerialActorJoinHandler.started.complete(null);
            SerialActorJoinHandler.release.join();
            SerialActorJoinHandler.events.add("join:end:" + actor.actorId());
            return ZLinkSpotActorJoinResponse.accept(
                Message.from(("joined:" + decoded.value()).getBytes(StandardCharsets.UTF_8)));
        }
    }

    public static final class SerialSpotHandler
        implements ZLinkSpotPacketHandler<SerialSpot, String> {
        static final List<String> events = new CopyOnWriteArrayList<>();
        static CompletableFuture<Void> releaseFirst = new CompletableFuture<>();
        static CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        static CompletableFuture<Void> secondStarted = new CompletableFuture<>();

        static void reset() {
            events.clear();
            releaseFirst = new CompletableFuture<>();
            firstStarted = new CompletableFuture<>();
            secondStarted = new CompletableFuture<>();
        }

        @Override
        public void handle(SerialSpot spot, String message) {
            events.add("start:" + message);
            if ("first".equals(message)) {
                firstStarted.complete(null);
                releaseFirst.join();
                events.add("end:first");
                return;
            }
            events.add("end:" + message);
            secondStarted.complete(null);
                    }
    }

    @ZLinkPacket("SerialJoinRequest")
    public static final class SerialJoinRequest {
        private final String value;

        public SerialJoinRequest(String value) {
            this.value = value;
        }

        public String value() {
            return value;
        }
    }

    public static final class SerialActorJoinHandler {
        static final List<String> events = new CopyOnWriteArrayList<>();
        static CompletableFuture<Void> started = new CompletableFuture<>();
        static CompletableFuture<Void> release = new CompletableFuture<>();

        static void reset() {
            events.clear();
            started = new CompletableFuture<>();
            release = new CompletableFuture<>();
        }

        public SerialActorJoinHandler() {
        }
    }

    public static final class SerialJoinSerializer implements ZLinkMessageSerializer {
        @Override
        public <T> Message serialize(T value) {
            if (value instanceof Message message) {
                return Message.from(message);
            }
            if (value instanceof byte[] bytes) {
                return Message.from(bytes);
            }
            if (value instanceof SerialJoinRequest request) {
                return Message.from(request.value().getBytes(StandardCharsets.UTF_8));
            }
            return Message.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(Message message, Class<T> type) {
            if (type == Message.class) {
                return type.cast(Message.from(message));
            }
            if (type == byte[].class) {
                return type.cast(message.toByteArray());
            }
            if (type == String.class) {
                return type.cast(message.toUtf8String());
            }
            if (type == SerialJoinRequest.class) {
                return type.cast(new SerialJoinRequest(message.toUtf8String()));
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
        }
    }

    public static final class NoopSendHandler
        implements ZLinkSendHandler<String> {
        @Override
        public void handle(
            String message,
            ZLinkSendContext context) {
                    }
    }

    public static final class AmbientOutboundSpot implements ZLinkSpot {
        static ZLinkSpotOutbound outbound;

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public ZLinkSpotCreateResponse onCreate(Message request) {
            outbound
                .sendToChannel("play-events", "hello")
                .packetName("Greeting")
                .submit()
                .toCompletableFuture()
                .join();
            return ZLinkSpotCreateResponse.accept();
        }
    }

    public static final class LifecycleEntrySpot implements ZLinkEntrySpot {
        static final AtomicInteger configureCount = new AtomicInteger();
        static final AtomicInteger initializeCount = new AtomicInteger();
        static final AtomicInteger closingCount = new AtomicInteger();
        static final AtomicReference<String> lastJoin = new AtomicReference<>();
        static final AtomicReference<String> lastLeave = new AtomicReference<>();
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
        public void onInitialize() {
            initializeCount.incrementAndGet();
                    }

        @Override
        public void onClosing() {
            closingCount.incrementAndGet();
                    }

        @Override
        public void onJoinActor(
            ZLinkActor actor,
            CancellationToken cancellationToken) {
            lastJoin.set(actor.actorId() + ":" + actor.context().isJoined());
                    }

        @Override
        public void onLeaveActor(
            ZLinkActor actor,
            CancellationToken cancellationToken) {
            lastLeave.set(actor.actorId() + ":" + actor.context().isJoined());
                    }
    }

    public static final class InterfaceEntrySpot implements ZLinkEntrySpot {
        private final ZLinkEntrySpotContext context;

        public InterfaceEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(InterfaceEntryActorRequestHandler.class);
        }
    }

    public static final class InterfaceEntryActorRequestHandler
        implements ZLinkEntrySpotActorRequestHandler<
            InterfaceEntrySpot,
            PlayerActor,
            InterfaceActorRequest,
            String> {
        static final AtomicReference<String> lastRequest = new AtomicReference<>();

        @Override
        public String handle(
            InterfaceEntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            InterfaceActorRequest request,
            systems.zlink.framework.CancellationToken cancellationToken) {
            lastRequest.set(actor.actorId() + ":" + request.value());
            return "reply:" + request.value();
        }
    }

    public static final class InterfaceUserSpot implements ZLinkSpot {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();
        static final AtomicReference<String> lastPostJoin = new AtomicReference<>();
        private final ZLinkSpotContext context;

        public InterfaceUserSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
        }

        @Override
        public ZLinkSpotActorJoinResponse onActorJoin(
            ZLinkActor actor,
            Message request,
            CancellationToken cancellationToken) {
            lastJoin.set(actor.actorId() + ":" + request.toUtf8String());
            return
                ZLinkSpotActorJoinResponse.accept(Message.from("joined:" + request.toUtf8String()));
        }

        @Override
        public void onJoinActor(
            ZLinkActor actor,
            CancellationToken cancellationToken) {
            lastPostJoin.set(actor.actorId() + ":" + actor.context().isJoined());
                    }
    }

    @ZLinkPacket("InterfaceActorRequest")
    public record InterfaceActorRequest(String value) {
    }

    public static final class InterfaceActorSerializer implements ZLinkMessageSerializer {
        @Override
        public <T> Message serialize(T value) {
            if (value instanceof Message message) {
                return Message.from(message);
            }
            if (value instanceof InterfaceActorRequest request) {
                return Message.from(request.value().getBytes(StandardCharsets.UTF_8));
            }
            return Message.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(Message message, Class<T> type) {
            if (type == Message.class) {
                return type.cast(Message.from(message));
            }
            if (type == String.class) {
                return type.cast(message.toUtf8String());
            }
            if (type == InterfaceActorRequest.class) {
                return type.cast(new InterfaceActorRequest(message.toUtf8String()));
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorSendHandler {
        static final AtomicReference<String> lastMessage = new AtomicReference<>();

        @ZLinkSpotActorSend
        public void send(PlayerActor actor, String message) {
            lastMessage.set(actor.actorId() + ":" + message);
        }
    }

    @ZLinkHandlerGroup("entry")
    public static final class ActorRequestHandler {
        static final AtomicReference<String> lastRequest = new AtomicReference<>();

        @ZLinkSpotActorRequest(packetName = "ActorRequestString")
        public String request(PlayerActor actor, String request) {
            lastRequest.set(actor.actorId() + ":" + request);
            return "reply:" + request;
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
        public ZLinkActor create(
            String actorId,
            ZLinkActorContext context) {
            return new PlayerActor(actorId, context);
        }
    }

    public record SpotGreeting(String value) {
    }

    @ZLinkPacket("SpotQuestion")
    public record SpotQuestion(String value) {
    }
}
