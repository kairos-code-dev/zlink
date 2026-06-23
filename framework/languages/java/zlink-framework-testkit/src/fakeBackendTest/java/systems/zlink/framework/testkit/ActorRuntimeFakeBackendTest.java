package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamError;

final class ActorRuntimeFakeBackendTest {
    @Test
    void actorManagerCreateGetOrCreateAndFindUseRegisteredFactory() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(GameSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor created = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
            ZLinkActor reused = runtime.actorManager()
                .getOrCreate("player-1", "player")
                .toCompletableFuture()
                .join();
            Optional<ZLinkActor> found = runtime.actorManager()
                .find("player-1")
                .toCompletableFuture()
                .join();

            assertSame(created, reused);
            assertEquals(Optional.of(created), found);
            assertEquals("player-1", created.actorId());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.createActor.player-1",
                "close.context",
                "spotNode.destroyActor.player-1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void actorContextJoinEntrySpotUsesBackendSpotNodeJoinOperation() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.addEntrySpot(EntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"), ZLinkMessage.empty())
                .submit(Message.class)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", joined.actor().actorId());
            assertEquals(RoutingId.from("entry-node"), joined.actor().nodeRid());
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActorEntrySpot.player-1.entry-node"));
    }

    @Test
    void actorContextJoinSpotUsesBackendSpotNodeJoinOperationAndUpdatesContext() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(GameSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId spotRid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            var joined = actor.context()
                .joinSpot(spotRid, "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(0, joined.resultCode());
            assertEquals("joined", joined.reply());
            assertEquals("player-1", joined.actor().actorId());
            assertEquals(RoutingId.from("spot-node"), joined.actor().nodeRid());
            assertEquals(Optional.of(spotRid), actor.context().spotRid());
            assertSame(GameSpot.instance, actor.context().getSpot(GameSpot.class));

            actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"), ZLinkMessage.empty())
                .submit(Message.class)
                .toCompletableFuture()
                .join();
            assertThrows(
                ZLinkConfigurationException.class,
                actor.context()::getSpot);
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActor.player-1.spot-node.game-1"));
        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActorEntrySpot.player-1.entry-node"));
    }

    @Test
    void customCodecActorContextJoinEncodesRequestAndDecodesReplyThroughRegistry() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(CustomCodecJoinSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        CustomJoinSerializer serializer = new CustomJoinSerializer();
        RoutingId spotRid = RoutingId.from("custom-codec-room");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.newFrameworkRuntime(
                     options,
                     backendFactory,
                     serializer,
                     ZLinkHandlerFactory.reflection())) {
            runtime.spotManager()
                .create(CustomCodecJoinSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = runtime.actorManager()
                .create("player-custom", "player")
                .toCompletableFuture()
                .join();

            ZLinkActorJoinResult<CustomJoinReply> joined = actor.context()
                .joinSpot(spotRid, new CustomJoinRequest("custom"))
                .submit(CustomJoinReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(0, joined.resultCode());
            assertEquals("custom", serializer.lastJoinRequest.get());
            assertEquals("joined", joined.reply().value());
        }
    }

    @Test
    void entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback() {
        EntrySpot.createCount = 0;
        EntrySpot.leftCount = 0;
        EntrySpot.disconnectCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router");
                node.addEntrySpot(EntrySpot.class);
                node.addSpotFactory(GameSpot.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.attachActorGateway("play");
            stream.registerSession(DestroySession.class); };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId spotRid = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor entryOwned = runtime.actorManager()
                .create("player-destroy", "player")
                .toCompletableFuture()
                .join();
            assertEquals(1, EntrySpot.createCount);
            ((ZLinkActorRuntime) runtime.actorManager())
                .notifyDisconnected(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(1, EntrySpot.disconnectCount);
            assertSame(
                entryOwned,
                runtime.actorManager().find("player-destroy").toCompletableFuture().join().orElseThrow());
            var entrySessionActors = runtime.sessionActors(
                "gateway",
                RoutingId.from("session-destroy"));
            entrySessionActors.bind(entryOwned).toCompletableFuture().join();
            assertTrue(entrySessionActors.find("player-destroy").isPresent());

            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy").toCompletableFuture().join());
            assertThrows(
                ZLinkConfigurationException.class,
                () -> entryOwned.context().boundSession());
            assertEquals(Optional.empty(), entrySessionActors.find("player-destroy"));
            boolean[] staleDispatchRan = {false};
            assertThrows(
                CompletionException.class,
                () -> ((ZLinkActorRuntime) runtime.actorManager())
                    .submitActorDispatch(
                        "player-destroy",
                        () -> {
                            staleDispatchRan[0] = true;
                            return java.util.concurrent.CompletableFuture.completedFuture(null);
                        })
                    .toCompletableFuture()
                    .join());
            assertEquals(false, staleDispatchRan[0]);
            assertEquals(0, EntrySpot.leftCount);

            ZLinkActor recreated = runtime.actorManager()
                .create("player-destroy", "player")
                .toCompletableFuture()
                .join();
            assertEquals(2, EntrySpot.createCount);
            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            assertSame(
                recreated,
                runtime.actorManager().find("player-destroy").toCompletableFuture().join().orElseThrow());
            assertEquals(0, EntrySpot.leftCount);

            PlayerActor reentrant = (PlayerActor) runtime.actorManager()
                .create("player-destroy-reentrant", "player")
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActor(reentrant)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy-reentrant").toCompletableFuture().join());
            assertEquals(0, EntrySpot.leftCount);

            runtime.spotManager()
                .create(GameSpot.class, spotRid)
                .toCompletableFuture()
                .join();
            ZLinkActor roomActor = runtime.actorManager()
                .create("player-room-destroy", "player")
                .toCompletableFuture()
                .join();
            roomActor.context()
                .joinSpot(spotRid, "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertThrows(
                CompletionException.class,
                () -> EntrySpot.instance.context()
                    .destroyActor(roomActor)
                    .toCompletableFuture()
                    .join());

            roomActor.context()
                .joinEntrySpot(RoutingId.from("spot-node"), ZLinkMessage.empty())
                .submit(Message.class)
                .toCompletableFuture()
                .join();
            EntrySpot.instance.context()
                .destroyActor(roomActor)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-room-destroy").toCompletableFuture().join());
            assertEquals(0, EntrySpot.leftCount);
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.destroyActor.player-destroy"));
        assertEquals(
            true,
            backendFactory.calls().contains("stream.unbindActor.player-destroy"));
        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.destroyActor.player-room-destroy"));
    }

    @Test
    void actorJoinSpotUsesRemoteAddressResolverWhenSpotIsNotLocal() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotRemoteAddressResolver(RemoteRoomResolver.class);
        { var route = options.addRouteMeshChannel("rooms"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://remote-route");
            route.enableSpotRouteEgress("rooms"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://local-router");
                node.connectRouter(RoutingId.from("remote-node"), "inproc://remote-router");
                node.acceptSpotRoutesFromChannel("rooms", "inproc://remote-route"); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-remote-join", "player")
                .toCompletableFuture()
                .join();

            ZLinkActorJoinResult<String> joined = actor.context()
                .joinSpot(RoutingId.from("remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(0, joined.resultCode());
            assertEquals("joined", joined.reply());
            assertEquals(RoutingId.from("remote-node"), joined.actor().nodeRid());
        }

        assertTrue(backendFactory.calls().contains(
            "spotRouteBridge.bridge.request.rooms.remote-room.__zlink.actor.joinSpot"),
            () -> "calls: " + backendFactory.calls());
        assertEquals(
            false,
            backendFactory.calls().stream().anyMatch(call -> call.startsWith("spotNode.joinActor.")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void remoteRoutedActorJoinRejectDoesNotDeserializeEmptyReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotRemoteAddressResolver(RemoteRoomResolver.class);
        { var route = options.addRouteMeshChannel("rooms"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://remote-route");
            route.enableSpotRouteEgress("rooms"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://local-router");
                node.connectRouter(RoutingId.from("remote-node"), "inproc://remote-router");
                node.acceptSpotRoutesFromChannel("rooms", "inproc://remote-route"); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-reject-join", "player")
                .toCompletableFuture()
                .join();

            CompletionException error = assertThrows(
                CompletionException.class,
                () -> actor.context()
                    .joinSpot(RoutingId.from("remote-room"), "join-request")
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());

            assertTrue(error.getCause() instanceof ZLinkConfigurationException);
            assertEquals("actor spot join rejected: 1", error.getCause().getMessage());
        }
    }

    @Test
    void actorJoinSpotUsesRegistrySpotResolverWithoutExplicitRouteEgress() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("inproc://registry"); };
        { var route = options.addRouteMeshChannel("rooms"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://source-route"); };
        { var mesh = options.addSpotMesh("game").useRegistrySpotResolver(); { var node = mesh.addNode("play"); node.enableRouter("inproc://local-router"); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-registry-remote-join", "player")
                .toCompletableFuture()
                .join();

            ZLinkActorJoinResult<String> joined = actor.context()
                .joinSpot(RoutingId.from("remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(0, joined.resultCode());
            assertEquals("joined", joined.reply());
            assertEquals(RoutingId.from("node"), joined.actor().nodeRid());
        }

        assertTrue(backendFactory.calls().contains(
            "spotRouteBridge.bridge.request.rooms.remote-room.__zlink.actor.joinSpot"),
            () -> "calls: " + backendFactory.calls());
        assertTrue(backendFactory.calls().contains(
            "discovery.game.setSpotOwnerSyncEnabled.true"),
            () -> "calls: " + backendFactory.calls());
        assertEquals(
            false,
            backendFactory.calls().stream().anyMatch(call -> call.startsWith("spotNode.joinActor.")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void boundManagedActorRoutesPacketsToRemoteJoinedSpotInsteadOfNativeGateway() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotRemoteAddressResolver(RemoteRoomResolver.class);
        { var route = options.addRouteMeshChannel("rooms"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://remote-route");
            route.enableSpotRouteEgress("rooms"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://local-router");
                node.connectRouter(RoutingId.from("remote-node"), "inproc://remote-router");
                node.acceptSpotRoutesFromChannel("rooms", "inproc://remote-route"); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.attachActorGateway("play");
            stream.registerSession(DestroySession.class); };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-remote-relay", "player")
                .toCompletableFuture()
                .join();
            var sessionActor = runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();
            actor.context()
                .joinSpot(RoutingId.from("remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            sessionActor.relay(
                    new ZLinkStreamHeader("MoveReq", java.util.Map.of(), Optional.empty()),
                    ZLinkMessage.of("move"))
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains(
            "spotRouteBridge.bridge.send.rooms.remote-room.__zlink.actor.packet"),
            () -> "calls: " + backendFactory.calls());
        assertEquals(
            false,
            backendFactory.calls().contains("relayBoundActor.player-remote-relay.JSON.MoveReq"),
            () -> "calls: " + backendFactory.calls());
        assertEquals(
            true,
            backendFactory.calls().contains("stream.bindActor.player-remote-relay"),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void nativeRemoteActorJoinRebindsExistingBoundSession() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.addSpotFactory(GameSpot.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.attachActorGateway("play");
            stream.registerSession(DestroySession.class); };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-native-remote", "player")
                .toCompletableFuture()
                .join();
            runtime.sessionActors("gateway", RoutingId.from("session-native"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            ZLinkActorJoinResult<String> joined = actor.context()
                .joinSpot(RoutingId.from("native-remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(RoutingId.from("native-remote-node"), joined.actor().nodeRid());
            assertEquals(Optional.of(RoutingId.from("native-remote-room")), actor.context().spotRid());
        }

        assertEquals(
            2,
            backendFactory.calls().stream()
                .filter(call -> call.equals("stream.bindActor.player-native-remote"))
                .count(),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void remoteRoutedActorJoinBindsNativeBoundSessionSend() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var route = options.addRouteMeshChannel("rooms"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://source-route"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://local-router");
                node.acceptSpotRoutesFromChannel("rooms", "inproc://local-route");
                node.addSpotFactory(NotifyingJoinSpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        RoutingId roomRid = RoutingId.from("remote-room");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(NotifyingJoinSpot.class, roomRid)
                .toCompletableFuture()
                .join();
            try (Message joinPacket = Message.from(ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME);
                 Message joinRequest = ZLinkActorSpotRoutePackets.encodeJoinRequest(
                    "player-routed-bound",
                    "player",
                    new ZLinkBackendActorRef(
                        RoutingId.from("source-actor-node"),
                        "player-routed-bound",
                        7),
                    RoutingId.from("source-entry"),
                    RoutingId.from("source-session-node"),
                    RoutingId.from("source-session"));
                 Message joinPayload = Message.from(new byte[0])) {
                backendFactory.dispatchRouteMeshSpotRequest(
                    RoutingId.from("source"),
                    roomRid,
                    List.of(
                        joinPacket,
                        joinRequest,
                        joinPayload),
                    1);
            }
            awaitCall(
                backendFactory,
                "spotRouteBridge.bridge.handleRouterReceived.rooms.__zlink.actor.joinSpot");
        }

        assertTrue(backendFactory.calls().stream().anyMatch(call ->
                call.startsWith(
                    "spotRouteBridge.bridge.handleRouterReceived.rooms.__zlink.actor.joinSpot")),
            () -> "calls: " + backendFactory.calls());
        assertEquals(
            false,
            backendFactory.calls().stream().anyMatch(call ->
                call.startsWith("spotRouteBridge.bridge.request.rooms.room-remote.__zlink.actor.bound_session.send")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void actorCreateDoesNotNotifyEntrySpotOwnedByDifferentNode() {
        SecondEntrySpot.createCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("first"); node.setRouterRoutingId(RoutingId.from("first-node")); };
            { var node = mesh.addNode("second"); node.setRouterRoutingId(RoutingId.from("second-node"));
                node.addEntrySpot(SecondEntrySpot.class); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.actorManager()
                .create("player-owned-by-second", "player")
                .toCompletableFuture()
                .join();

            assertEquals(0, SecondEntrySpot.createCount);
        }
    }

    @Test
    void actorEntrySpotRouteJoinHandlerCreatesLocalActorAndReturnsActorRefReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.enableRouter("inproc://play-router"); }; };
        options.addActorFactory("player", PlayerActorFactory.class);
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActorRuntime actors = (ZLinkActorRuntime) runtime.actorManager();
            Message request = ZLinkActorEntrySpotRoutePackets.encodeJoinRequest(
                "player-remote",
                "player",
                RoutingId.from("source-node"),
                7);

            Message reply = actors.handleEntrySpotRouteJoin(
                    RoutingId.from("source-route"),
                    request)
                .toCompletableFuture()
                .join();
            var decoded = ZLinkActorEntrySpotRoutePackets.decodeJoinReply(reply);

            assertEquals("player-remote", decoded.actorId());
            assertEquals("player", decoded.actorType());
            assertEquals(RoutingId.from("spot-node"), decoded.targetNodeRid());
            assertEquals(0, decoded.actorGeneration());
            request.close();
            reply.close();
        }

        assertEquals(true, backendFactory.calls().contains("spotNode.createActor.player-remote"));
    }

    private static void awaitCall(
        FakeZLinkBackendAdapterFactory backendFactory,
        String expectedPrefix) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(5).toNanos();
        while (System.nanoTime() < deadline) {
            if (backendFactory.calls().stream().anyMatch(call -> call.startsWith(expectedPrefix))) {
                return;
            }
            Thread.onSpinWait();
        }
    }

    public static final class PlayerActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;
        boolean destroyAgainOnEntryLeft;

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

    public static final class RemoteRoomResolver implements ZLinkSpotRemoteAddressResolver {
        @Override
        public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(
            RoutingId spotRid) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                new ZLinkSpotRemoteAddress(
                    "rooms",
                    RoutingId.from("remote-node"),
                    spotRid,
                    ZLinkSpotKind.USER));
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

    public static final class GameSpot implements ZLinkSpot<ZLinkActor> {
        static GameSpot instance;

        public GameSpot() {
            instance = this;
        }

        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class CustomCodecJoinSpot implements ZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public ZLinkSpotActorJoinResponse onActorJoin(
            ZLinkActor actor,
            ZLinkMessage request,
            CancellationToken cancellationToken) {
            CustomJoinRequest decoded = request.decode(CustomJoinRequest.class);
            lastJoin.set(actor.actorId() + ":" + decoded.value());
            return ZLinkSpotActorJoinResponse.accept(new CustomJoinReply("reply:" + decoded.value()));
        }
    }

    public record CustomJoinRequest(String value) {
    }

    public record CustomJoinReply(String value) {
    }

    public static final class CustomJoinSerializer implements ZLinkMessageSerializer {
        final AtomicReference<String> lastJoinRequest = new AtomicReference<>();

        @Override
        public <T> Message serialize(T value) {
            if (value instanceof Message message) {
                return Message.from(message);
            }
            if (value instanceof byte[] bytes) {
                return Message.from(bytes);
            }
            if (value instanceof CustomJoinRequest request) {
                lastJoinRequest.set(request.value());
                return Message.from(("req:" + request.value()).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            if (value instanceof CustomJoinReply reply) {
                return Message.from(("rep:" + reply.value()).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            return Message.from(String.valueOf(value).getBytes(java.nio.charset.StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(Message message, Class<T> type) {
            String text = message.toUtf8String();
            if (type == Message.class) {
                return type.cast(Message.from(message));
            }
            if (type == byte[].class) {
                return type.cast(message.toByteArray());
            }
            if (type == String.class) {
                return type.cast(text);
            }
            if (type == CustomJoinRequest.class && text.startsWith("req:")) {
                return type.cast(new CustomJoinRequest(text.substring(4)));
            }
            if (type == CustomJoinReply.class) {
                return type.cast(new CustomJoinReply(text.startsWith("rep:") ? text.substring(4) : text));
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
        }
    }

    public static final class NotifyingJoinSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public ZLinkSpotActorJoinResponse onActorJoin(
            ZLinkActor actor,
            ZLinkMessage request,
            CancellationToken cancellationToken) {
            ZLinkAwait.await(actor.context().boundSession().send("joined-notify").submit());
            return ZLinkSpotActorJoinResponse.accept("joined");
        }
    }

    public static final class EntrySpot implements ZLinkEntrySpot<ZLinkActor> {
        static EntrySpot instance;
        static int createCount;
        static int leftCount;
        static int disconnectCount;
        private final ZLinkEntrySpotContext context;

        public EntrySpot(ZLinkEntrySpotContext context) {
            instance = this;
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void onCreateActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            createCount++;
        }

        @Override
        public void onLeaveActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            leftCount++;
        }

        @Override
        public void onDisconnectActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            disconnectCount++;
        }
    }

    public static final class SecondEntrySpot implements ZLinkEntrySpot<ZLinkActor> {
        static int createCount;
        private final ZLinkEntrySpotContext context;

        public SecondEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void onCreateActor(
            ZLinkActor actor,
            systems.zlink.framework.CancellationToken cancellationToken) {
            createCount++;
        }
    }

    public static final class DestroySession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public void onConnected() {
        }

        @Override
        public void onDisconnected() {
        }

        @Override
        public void onError(ZLinkStreamError error) {
        }
    }
}
