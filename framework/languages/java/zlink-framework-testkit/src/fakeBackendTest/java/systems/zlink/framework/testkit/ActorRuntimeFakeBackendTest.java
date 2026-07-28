package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.internal.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletableFuture;
import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.atomic.AtomicReference;
import com.google.protobuf.StringValue;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class ActorRuntimeFakeBackendTest {
    @Test
    void actorManagerCreateGetOrCreateAndFindUseRegisteredFactory() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://actor-manager-router");
                node.addSpotFactory(GameSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            var created = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
            var reused = runtime.actorManager()
                .getOrCreate("player-1", "player")
                .toCompletableFuture()
                .join();
            var found = runtime.actorManager()
                .find("player-1")
                .toCompletableFuture()
                .join();

            assertEquals(created, reused);
            assertEquals(Optional.of(created), found);
            assertEquals("player-1", created.actorId());
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://actor-manager-router",
                "spotNode.entrySpot",
                "create.entrySpot",
                "spotNode.createActor.player-1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void actorContextJoinEntrySpotUsesBackendSpotNodeJoinOperation() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://play-router");
                node.addEntrySpot(EntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            long sourceGeneration = ((ZLinkActorRuntime) runtime.actorManager())
                .actorRef(actor)
                .generation();

            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"), ZLinkMessage.empty())
                .submit(Message.class)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", accepted(joined).actor().actorId());
            assertEquals(RoutingId.from("entry-node"), accepted(joined).actor().nodeRid());
            assertEquals(sourceGeneration, accepted(joined).actor().generation());
        }

        assertEquals(
            true,
            backendFactory.calls().contains("spotNode.joinActorEntrySpot.player-1.entry-node"));
    }

    @Test
    void actorContextJoinSpotUsesBackendSpotNodeJoinOperationAndUpdatesContext() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://actor-join-router");
                node.addSpotFactory(GameSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        String spotId = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(GameSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            long sourceGeneration = ((ZLinkActorRuntime) runtime.actorManager())
                .actorRef(actor)
                .generation();

            var joined = actor.context()
                .joinSpot(spotId, "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertInstanceOf(ZLinkActorJoinResult.Accepted.class, joined);
            assertEquals("joined", joined.reply());
            assertEquals("player-1", accepted(joined).actor().actorId());
            assertEquals(RoutingId.from("spot-node"), accepted(joined).actor().nodeRid());
            assertEquals(sourceGeneration, accepted(joined).actor().generation());
            assertEquals(Optional.of(spotId), actor.context().spotId());
            assertEquals(Optional.of(spotId), actor.context().spotId());

            actor.context()
                .joinEntrySpot(RoutingId.from("entry-node"), ZLinkMessage.empty())
                .submit(Message.class)
                .toCompletableFuture()
                .join();
            assertEquals(Optional.of(RoutingId.from("entry-node")), actor.context().spotId());
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
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://actor-custom-codec-router");
                node.addSpotFactory(CustomCodecJoinSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        CustomJoinSerializer serializer = new CustomJoinSerializer();
        String spotId = RoutingId.from("custom-codec-room");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.newFrameworkRuntime(
                     options,
                     backendFactory,
                     serializer,
                     ZLinkHandlerActivator.reflection())) {
            Message reply = messageFrom(serializer.serialize(new CustomJoinReply("joined")));
            try {
                backendFactory.nextActorJoinReply(reply);
            } finally {
                reply.close();
            }
            runtime.spotManager()
                .create(CustomCodecJoinSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-custom", "player");

            ZLinkActorJoinResult<CustomJoinReply> joined = actor.context()
                .joinSpot(spotId, new CustomJoinRequest("custom"))
                .submit(CustomJoinReply.class)
                .toCompletableFuture()
                .join();

            assertInstanceOf(ZLinkActorJoinResult.Accepted.class, joined);
            assertEquals("custom", serializer.lastJoinRequest.get());
            assertEquals("joined", joined.reply().value());
        }
    }

    @Test
    void protobufActorContextJoinEncodesRequestAndDecodesReplyThroughRegistry() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.codecs().use(ZLinkProtobufCodec.defaultCodec());
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://actor-protobuf-router");
                node.addSpotFactory(ProtobufJoinSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkMessageSerializer serializer = serializerWith(ZLinkProtobufCodec.defaultCodec());
        String spotId = RoutingId.from("protobuf-codec-room");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            Message reply = messageFrom(serializer.serialize(StringValue.of("reply:proto")));
            try {
                backendFactory.nextActorJoinReply(reply);
            } finally {
                reply.close();
            }
            runtime.spotManager()
                .create(ProtobufJoinSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-protobuf", "player");

            ZLinkActorJoinResult<StringValue> joined = actor.context()
                .joinSpot(spotId, StringValue.of("proto"))
                .submit(StringValue.class)
                .toCompletableFuture()
                .join();

            assertInstanceOf(ZLinkActorJoinResult.Accepted.class, joined);
            assertEquals("reply:proto", joined.reply().getValue());
        }
    }

    @Test
    void messagePackActorContextJoinEncodesRequestAndDecodesReplyThroughRegistry() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.codecs().use(ZLinkMessagePackCodec.defaultCodec());
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://actor-messagepack-router");
                node.addSpotFactory(MessagePackJoinSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkMessageSerializer serializer = serializerWith(ZLinkMessagePackCodec.defaultCodec());
        String spotId = RoutingId.from("messagepack-codec-room");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            Message reply = messageFrom(serializer.serialize(new PackedJoinReply("reply:msgpack")));
            try {
                backendFactory.nextActorJoinReply(reply);
            } finally {
                reply.close();
            }
            runtime.spotManager()
                .create(MessagePackJoinSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-messagepack", "player");

            ZLinkActorJoinResult<PackedJoinReply> joined = actor.context()
                .joinSpot(spotId, new PackedJoinRequest("msgpack"))
                .submit(PackedJoinReply.class)
                .toCompletableFuture()
                .join();

            assertInstanceOf(ZLinkActorJoinResult.Accepted.class, joined);
            assertEquals("reply:msgpack", joined.reply().value());
        }
    }

    @Test
    void entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback() {
        EntrySpot.createCount = 0;
        EntrySpot.leftCount = 0;
        EntrySpot.disconnectCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://play-router");
                node.addEntrySpot(EntrySpot.class);
                node.addSpotFactory(GameSpot.class);
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.registerSession(DestroySession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        String spotId = RoutingId.from("game-1");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor entryOwned = managedActor(runtime, "player-destroy", "player");
            assertEquals(1, EntrySpot.createCount);
            ((ZLinkActorRuntime) runtime.actorManager())
                .notifyDisconnected(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(1, EntrySpot.disconnectCount);
            assertEquals(
                entryOwned.actorId(),
                runtime.actorManager()
                    .find("player-destroy")
                    .toCompletableFuture()
                    .join()
                    .orElseThrow()
                    .actorId());
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

            ZLinkActor recreated = managedActor(runtime, "player-destroy", "player");
            assertEquals(2, EntrySpot.createCount);
            EntrySpot.instance.context()
                .destroyActor(entryOwned)
                .toCompletableFuture()
                .join();
            assertEquals(
                recreated.actorId(),
                runtime.actorManager()
                    .find("player-destroy")
                    .toCompletableFuture()
                    .join()
                    .orElseThrow()
                    .actorId());
            assertEquals(0, EntrySpot.leftCount);

            PlayerActor reentrant = (PlayerActor) managedActor(runtime, "player-destroy-reentrant", "player");
            EntrySpot.instance.context()
                .destroyActor(reentrant)
                .toCompletableFuture()
                .join();
            assertEquals(
                Optional.empty(),
                runtime.actorManager().find("player-destroy-reentrant").toCompletableFuture().join());
            assertEquals(0, EntrySpot.leftCount);

            runtime.spotManager()
                .create(GameSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor roomActor = managedActor(runtime, "player-room-destroy", "player");
            roomActor.context()
                .joinSpot(spotId, "join-request")
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
        options.addLocationStore(remoteRoomStore());
        { var route = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addRouteMeshChannel(options, "game"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://remote-route");};
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://local-router");
                node.connectRouter(RoutingId.from("remote-node"), "inproc://remote-router");node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = managedActor(runtime, "player-remote-join", "player");
            long sourceGeneration = ((ZLinkActorRuntime) runtime.actorManager())
                .actorRef(actor)
                .generation();

            ZLinkActorJoinResult<String> joined = actor.context()
                .joinSpot(RoutingId.from("remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertInstanceOf(ZLinkActorJoinResult.Accepted.class, joined);
            assertEquals("joined", joined.reply());
            assertEquals(RoutingId.from("remote-node"), accepted(joined).actor().nodeRid());
            assertEquals(sourceGeneration, accepted(joined).actor().generation());
        }

        assertTrue(backendFactory.calls().contains(
            "spotRouteBridge.bridge.request.rooms.remote-node.remote-room.__zlink.actor.joinSpot"),
            () -> "calls: " + backendFactory.calls());
        assertEquals(
            false,
            backendFactory.calls().stream().anyMatch(call -> call.startsWith("spotNode.joinActor.")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void remoteRoutedActorJoinRejectDoesNotDeserializeEmptyReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(remoteRoomStore());
        { var route = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addRouteMeshChannel(options, "game"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://remote-route");};
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://local-router");
                node.connectRouter(RoutingId.from("remote-node"), "inproc://remote-router");node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = managedActor(runtime, "player-reject-join", "player");

            ZLinkActorJoinResult<String> result = actor.context()
                .joinSpot(RoutingId.from("remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            ZLinkActorJoinResult.Rejected<String> rejected = assertInstanceOf(
                ZLinkActorJoinResult.Rejected.class, result);
            assertEquals(null, rejected.reply());
        }
    }

    @Test
    void boundManagedActorRoutesPacketsToRemoteJoinedSpotInsteadOfNativeGateway() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addLocationStore(remoteRoomStore());
        { var route = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addRouteMeshChannel(options, "game"); route.enableServer("inproc://local-route");
            route.enableClient("inproc://remote-route");};
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://local-router");
                node.connectRouter(RoutingId.from("remote-node"), "inproc://remote-router");
                node.addActorFactory("player", PlayerActorFactory.class);}; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.registerSession(DestroySession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = managedActor(runtime, "player-remote-relay", "player");
            var sessionActor = runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();
            actor.context()
                .joinSpot(RoutingId.from("remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(
                false,
                backendFactory.calls().contains(
                    "stream.unbindActor.player-remote-relay"),
                () -> "Core transfer must retain the source STREAM binding: "
                    + backendFactory.calls());

            relayWithHeader(sessionActor, "ActorNotify", ZLinkMessage.of("move"));
        }

        assertTrue(backendFactory.calls().contains(
            "stream.relayBoundActor.player-remote-relay.RAW.ActorNotify"),
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
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh;
                node.enableRouter("inproc://native-local-router")
                    .connectRouter(RoutingId.from("native-remote-node"), "inproc://native-remote-router");
                node.addSpotFactory(GameSpot.class);
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://fake-gateway");
            stream.registerSession(DestroySession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ZLinkActor actor = managedActor(runtime, "player-native-remote", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-native"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            ZLinkActorJoinResult<String> joined = actor.context()
                .joinSpot(RoutingId.from("native-remote-room"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals(RoutingId.from("native-remote-node"), accepted(joined).actor().nodeRid());
            assertEquals(Optional.of(RoutingId.from("native-remote-room")), actor.context().spotId());
        }

        assertEquals(
            2,
            backendFactory.calls().stream()
                .filter(call -> call.equals("stream.bindActor.player-native-remote"))
                .count(),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void actorCreateDoesNotNotifyEntrySpotOwnedByDifferentNode() {
        SecondEntrySpot.createCount = 0;
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "first"); { var node = mesh; node.enableRouter("inproc://first-node").setRoutingId(RoutingId.from("first-node"));
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "second"); { var node = mesh; node.enableRouter("inproc://second-node").setRoutingId(RoutingId.from("second-node"));
                node.addEntrySpot(SecondEntrySpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            managedActor(runtime, "player-owned-by-second", "player");

            assertEquals(0, SecondEntrySpot.createCount);
        }
    }

    @Test
    void actorEntrySpotRouteJoinHandlerCreatesLocalActorAndReturnsActorRefReply() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = systems.zlink.framework.runtime.internal.configuration.ZLinkLegacyTopology.addSpotMesh(options, "game"); { var node = mesh; node.enableRouter("inproc://play-router"); node.addActorFactory("player", PlayerActorFactory.class); }; };
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

    private static List<String> conciseCalls(FakeZLinkBackendAdapterFactory backendFactory) {
        List<String> calls = backendFactory.calls();
        int from = Math.max(0, calls.size() - 40);
        return calls.subList(from, calls.size());
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

    private static ZLinkInMemoryLocationStore remoteRoomStore() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        Instant now = Instant.now();
        store.renewOwnerLease("remote-owner", RoutingId.from("remote-node"), Duration.ofHours(1))
            .toCompletableFuture().join();
        store.updateSpot(new ZLinkSpotLocation(
            "game", RoutingId.from("remote-room"), "room", RoutingId.from("remote-node"),
            ZLinkSpotKind.USER, "inproc://remote-router", "remote-owner", 1, now),
            ZLinkLocationWriteIntent.TAKEOVER).toCompletableFuture().join();
        return store;
    }

    public static final class PlayerActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> create(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public static final class GameSpot extends TestZLinkSpot<ZLinkActor> {
        static GameSpot instance;

        public GameSpot() {
            instance = this;
        }

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept("joined"));
        }
    }

    public static final class CustomCodecJoinSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            CustomJoinRequest decoded = request.decode(CustomJoinRequest.class);
            lastJoin.set(actorId + ":" + decoded.value());
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept(new CustomJoinReply("reply:" + decoded.value())));
        }
    }

    public static final class ProtobufJoinSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            StringValue decoded = request.decode(StringValue.class);
            lastJoin.set(actorId + ":" + decoded.getValue());
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept(StringValue.of("reply:" + decoded.getValue())));
        }
    }

    public static final class MessagePackJoinSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastJoin = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            PackedJoinRequest decoded = request.decode(PackedJoinRequest.class);
            lastJoin.set(actorId + ":" + decoded.value());
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept(new PackedJoinReply("reply:" + decoded.value())));
        }
    }

    public record PackedJoinRequest(String value) {
    }

    public record PackedJoinReply(String value) {
    }

    public record CustomJoinRequest(String value) {
    }

    public record CustomJoinReply(String value) {
    }

    public static final class CustomJoinSerializer implements ZLinkMessageSerializer {
        final AtomicReference<String> lastJoinRequest = new AtomicReference<>();

        @Override
        public <T> ZLinkEncodedPayload serialize(T value) {
            if (value instanceof Message message) {
                return ZLinkEncodedPayload.from(message.toByteArray());
            }
            if (value instanceof byte[] bytes) {
                return ZLinkEncodedPayload.from(bytes);
            }
            if (value instanceof CustomJoinRequest request) {
                lastJoinRequest.set(request.value());
                return ZLinkEncodedPayload.from(("req:" + request.value()).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            if (value instanceof CustomJoinReply reply) {
                return ZLinkEncodedPayload.from(("rep:" + reply.value()).getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            return ZLinkEncodedPayload.from(String.valueOf(value).getBytes(java.nio.charset.StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
            String text = new String(payload.bytes(), java.nio.charset.StandardCharsets.UTF_8);
            if (type == Message.class) {
                return type.cast(Message.from(payload.bytes()));
            }
            if (type == byte[].class) {
                return type.cast(payload.bytes());
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

    private static ZLinkMessageSerializer serializerWith(
        systems.zlink.framework.configuration.ZLinkCodecExtension extension) {
        PublicCodecTestRegistry registration = new PublicCodecTestRegistry();
        registration.use(extension);
        return registration.serializerWithFallback(new ZLinkJsonMessageSerializer());
    }

    private static Message messageFrom(ZLinkEncodedPayload payload) {
        return Message.from(payload.bytes());
    }

    public static final class NotifyingJoinSpot extends TestZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept("joined"));
        }

        @Override
        public CompletionStage<Void> onJoinedActor(
            ZLinkActor actor) {
            actor.context().boundSession().send("joined-notify").submit();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class EntrySpot extends TestZLinkEntrySpot<ZLinkActor> {
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
        public CompletionStage<Void> onCreateActor(
            ZLinkActor actor,
            systems.zlink.framework.messaging.ZLinkMessage createRequest) {
            createCount++;
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(
            ZLinkActor actor) {
            leftCount++;
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectActor(
            ZLinkActor actor) {
            disconnectCount++;
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class SecondEntrySpot extends TestZLinkEntrySpot<ZLinkActor> {
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
        public CompletionStage<Void> onCreateActor(
            ZLinkActor actor,
            systems.zlink.framework.messaging.ZLinkMessage createRequest) {
            createCount++;
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class DestroySession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }
    }

    private static ZLinkActor managedActor(
        ZLinkFrameworkRuntime runtime,
        String actorId,
        String actorType) {
        return ((ZLinkActorRuntime) runtime.actorManager())
            .getOrCreateManagedActor(actorId, actorType)
            .toCompletableFuture()
            .join();
    }

    private static void relayWithHeader(
        ZLinkSessionActor actor,
        String packetName,
        ZLinkMessage payload) {
        ZLinkSessionActorsRuntime.enterRelayDispatch(
            new ZLinkStreamHeader(packetName, Map.of(), Optional.empty()));
        try {
            actor.relay(payload).toCompletableFuture().join();
        } finally {
            ZLinkSessionActorsRuntime.exitRelayDispatch();
        }
    }

    @SuppressWarnings("unchecked")
    private static <T> ZLinkActorJoinResult.Accepted<T> accepted(ZLinkActorJoinResult<T> result) {
        return (ZLinkActorJoinResult.Accepted<T>) assertInstanceOf(
            ZLinkActorJoinResult.Accepted.class, result);
    }
}
