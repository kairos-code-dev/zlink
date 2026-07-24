package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;

import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
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
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.SpotHandle;
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
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://spot-router");
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
                .map(info -> info.spotId()));
            assertEquals(List.of(rid), runtime.spotManager()
                .list()
                .toCompletableFuture()
                .join()
                .stream()
                .map(info -> info.spotId())
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

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.setPubBind.inproc://spot-pub",
                "spotNode.entrySpot",
                "create.entrySpot",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls().stream()
                .filter(call -> !call.startsWith("discovery.game.bindRoute."))
                .toList());
    }

    @Test
    void spotManagerCreatePayloadIsPassedToOnCreate() {
        PayloadSpot.lastCreatePayload.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://payload-router");
                node.addSpotFactory(PayloadSpot.class); }; };
        RoutingId rid = RoutingId.from("payload-spot");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new FakeZLinkBackendAdapterFactory())) {
            assertEquals(ZLinkSpotCreateState.CREATED, runtime.spotManager()
                .getOrCreate(PayloadSpot.class, rid, systems.zlink.framework.messaging.ZLinkMessage.of("first"))
                .toCompletableFuture()
                .join()
                .state());
            assertEquals("first", PayloadSpot.lastCreatePayload.get());

            assertEquals(ZLinkSpotCreateState.EXISTING, runtime.spotManager()
                .getOrCreate(PayloadSpot.class, rid, systems.zlink.framework.messaging.ZLinkMessage.of("second"))
                .toCompletableFuture()
                .join()
                .state());
            assertEquals("first", PayloadSpot.lastCreatePayload.get());
        }
    }

    @Test
    void spotManagerListAndCloseCanRunConcurrently() throws Exception {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://spot-router-concurrent");
                node.addSpotFactory(GameSpot.class); }; };
        List<RoutingId> rids = java.util.stream.IntStream.range(0, 32)
            .mapToObj(index -> RoutingId.from("game-concurrent-" + index))
            .toList();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new FakeZLinkBackendAdapterFactory())) {
            for (RoutingId rid : rids) {
                runtime.spotManager()
                    .create(GameSpot.class, rid)
                    .toCompletableFuture()
                    .join();
            }

            CountDownLatch start = new CountDownLatch(1);
            CompletableFuture<Void> closer = CompletableFuture.runAsync(() -> {
                awaitLatch(start);
                for (RoutingId rid : rids) {
                    runtime.spotManager().close(rid).toCompletableFuture().join();
                }
            });
            CompletableFuture<Void> lister = CompletableFuture.runAsync(() -> {
                awaitLatch(start);
                while (!closer.isDone()) {
                    runtime.spotManager().list().toCompletableFuture().join();
                }
            });

            start.countDown();
            CompletableFuture.allOf(closer, lister).get(2, TimeUnit.SECONDS);
            assertTrue(runtime.spotManager().list().toCompletableFuture().join().isEmpty());
        }
    }

    @Test
    void customCodecSpotCreateDecodesEncodedFrameworkMessage() {
        PayloadSpot.lastCreatePayload.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://payload-codec-router");
                node.addSpotFactory(PayloadSpot.class); }; };
        RoutingId rid = RoutingId.from("payload-codec-spot");
        CreatePayloadSerializer serializer = new CreatePayloadSerializer();
        Message encoded = messageFrom(serializer.serialize(new CreatePayload("custom")));

        try (encoded;
             ZLinkFrameworkRuntime runtime = RuntimeTestSupport.newFrameworkRuntime(
                 options,
                 new FakeZLinkBackendAdapterFactory(),
                 serializer,
                 ZLinkHandlerActivator.reflection())) {
            var created = runtime.spotManager()
                .getOrCreate(
                    PayloadSpot.class,
                    rid,
                    ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(encoded.toByteArray()), serializer))
                .toCompletableFuture()
                .join();
            assertEquals(ZLinkSpotCreateState.CREATED, created.state());
            assertEquals("custom", PayloadSpot.lastCreatePayload.get());
            Message reply = messageFrom(created.reply().toEncodedPayload(serializer));
            try {
                CreatePayload decoded = ZLinkMessage
                    .fromEncoded(ZLinkEncodedPayload.from(reply.toByteArray()), serializer)
                    .decode(CreatePayload.class);
                assertEquals("reply:custom", decoded.value());
            } finally {
                reply.close();
            }
        }
    }

    @Test
    void protobufSpotCreateDecodesRequestAndReplyThroughFrameworkMessage() {
        ProtobufCreateSpot.lastCreatePayload.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.codecs().use(ZLinkProtobufCodec.defaultCodec());
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://payload-protobuf-router");
                node.addSpotFactory(ProtobufCreateSpot.class); }; };
        RoutingId rid = RoutingId.from("payload-protobuf-spot");
        ZLinkMessageSerializer serializer = serializerWith(ZLinkProtobufCodec.defaultCodec());
        Message encoded = messageFrom(serializer.serialize(StringValue.of("proto-create")));

        try (encoded;
             ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(
                 options,
                 new FakeZLinkBackendAdapterFactory())) {
            var created = runtime.spotManager()
                .getOrCreate(
                    ProtobufCreateSpot.class,
                    rid,
                    ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(encoded.toByteArray()), serializer))
                .toCompletableFuture()
                .join();
            assertEquals(ZLinkSpotCreateState.CREATED, created.state());
            assertEquals("proto-create", ProtobufCreateSpot.lastCreatePayload.get());

            Message reply = messageFrom(created.reply().toEncodedPayload(serializer));
            try {
                StringValue decoded = ZLinkMessage
                    .fromEncoded(ZLinkEncodedPayload.from(reply.toByteArray()), serializer)
                    .decode(StringValue.class);
                assertEquals("reply:proto-create", decoded.getValue());
            } finally {
                reply.close();
            }
        }
    }

    @Test
    void messagePackSpotCreateDecodesRequestAndReplyThroughFrameworkMessage() {
        MessagePackCreateSpot.lastCreatePayload.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.codecs().use(ZLinkMessagePackCodec.defaultCodec());
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://payload-messagepack-router");
                node.addSpotFactory(MessagePackCreateSpot.class); }; };
        RoutingId rid = RoutingId.from("payload-messagepack-spot");
        ZLinkMessageSerializer serializer = serializerWith(ZLinkMessagePackCodec.defaultCodec());
        Message encoded = messageFrom(serializer.serialize(new PackedCreatePayload("msgpack-create")));

        try (encoded;
             ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(
                 options,
                 new FakeZLinkBackendAdapterFactory())) {
            var created = runtime.spotManager()
                .getOrCreate(
                    MessagePackCreateSpot.class,
                    rid,
                    ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(encoded.toByteArray()), serializer))
                .toCompletableFuture()
                .join();
            assertEquals(ZLinkSpotCreateState.CREATED, created.state());
            assertEquals("msgpack-create", MessagePackCreateSpot.lastCreatePayload.get());

            Message reply = messageFrom(created.reply().toEncodedPayload(serializer));
            try {
                PackedCreatePayload decoded = ZLinkMessage
                    .fromEncoded(ZLinkEncodedPayload.from(reply.toByteArray()), serializer)
                    .decode(PackedCreatePayload.class);
                assertEquals("reply:msgpack-create", decoded.value());
            } finally {
                reply.close();
            }
        }
    }

    @Test
    void spotOutboundChannelCallsUseSharedChannelClient() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var channel = options.addClientServerChannel("play-events"); channel.enableClient("inproc://play-events"); };
        { var channel = options.addClientServerChannel("play-rpc"); channel.enableClient("inproc://play-rpc"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://outbound-channel-router");
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
                .sendToChannel("play-events", new Greeting("hello"))
                .submit();
            OutboundSpot.context.outbound()
                .requestToChannel("play-rpc", new Ping("ping"))
                .submit(String.class)
                .toCompletableFuture()
                .join();
        }

        assertTrue(backendFactory.calls().contains("dealer.send.Greeting"));
        assertTrue(backendFactory.calls().contains("dealer.request.Ping"));
        assertFalse(backendFactory.calls().contains("spot.1.sendToChannel.play-events.Greeting"));
        assertFalse(backendFactory.calls().contains("spot.1.requestToChannel.play-rpc.Ping"));
    }

    @Test
    void spotContextDispatchesRegisteredPacketRequestAndSubscriptionHandlers() {
        HandlerSpot.dispatches.clear();
        HandlerSpot.metadata.clear();
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://handler-spot-router");
                node.enablePubSub("inproc://spot-pub");
                node.addSpotFactory(HandlerSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(HandlerSpot.class, RoutingId.from("handler-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotRoute(
                "String",
                "\"hello\"",
                metadataFrame("kind", "send"));
            backendFactory.dispatchSpotRequest(
                "SpotQuery",
                "\"ping\"",
                7,
                metadataFrame("kind", "request"));
            backendFactory.dispatchSpotSubscription(
                "stage.events",
                "String",
                "\"opened\"",
                metadataFrame("kind", "publish"));
            awaitCondition(() -> HandlerSpot.dispatches.size() == 3);
        }

        assertEquals(
            java.util.Set.of("packet:hello", "request:ping", "subscription:opened"),
            java.util.Set.copyOf(HandlerSpot.dispatches));
        assertEquals(List.of("\"reply:ping\""), backendFactory.spotReplies());
        assertEquals(
            Map.of(
                "packet", Map.of("kind", "send"),
                "request", Map.of("kind", "request"),
                "subscription", Map.of("kind", "publish")),
            HandlerSpot.metadata);
        assertThrows(
            UnsupportedOperationException.class,
            () -> HandlerSpot.metadata.get("request").put("late", "value"));
        assertTrue(backendFactory.calls().contains("spot.1.setSubscription.stage.events"));
    }

    @Test
    void userSpotKeepsItsTurnWhileRouteHandlerStageIsIncomplete() throws Exception {
        SerialSpotHandler.reset();
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://serial-release-router");
                node.addSpotFactory(SerialSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(SerialSpot.class, RoutingId.from("serial-spot"))
                .toCompletableFuture()
                .join();

            backendFactory.dispatchSpotRoute("String", "\"first\"");
            SerialSpotHandler.firstStarted.get(1, TimeUnit.SECONDS);
            backendFactory.dispatchSpotRoute("String", "\"second\"");

            assertThrows(
                java.util.concurrent.TimeoutException.class,
                () -> SerialSpotHandler.secondStarted.get(100, TimeUnit.MILLISECONDS));

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
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://serial-join-router");
                node.addSpotFactory(SerialSpot.class);
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();
        ZLinkHandlerActivator reflection = ZLinkHandlerActivator.reflection();
        ZLinkHandlerActivator handlerFactory = handlerType -> {
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
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://outbound-spot-router");
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
                .sendToSpot(targetSpotHandle(runtime), new Greeting("hello"))
                .metadata("trace-id", "send")
                .submit();
            awaitCall(
                backendFactory,
                "spot.1.sendToSpot.spot-node.target-spot.Greeting");
            assertEquals(
                Map.of("trace-id", "send"),
                ZLinkApplicationMetadata.decode(
                    backendFactory.lastApplicationMetadata()));

            Map<String, String> requestMetadata =
                new java.util.LinkedHashMap<>(Map.of("tenant", "blue"));
            var request = OutboundSpot.context.outbound()
                .requestToSpot(targetSpotHandle(runtime), new Ping("ping"))
                .metadata(requestMetadata)
                .metadata("tenant", "green");
            requestMetadata.put("tenant", "mutated");
            assertEquals(
                "reply",
                request.submit(String.class)
                    .toCompletableFuture()
                    .join());
            assertEquals(
                Map.of("tenant", "green"),
                ZLinkApplicationMetadata.decode(
                    backendFactory.lastApplicationMetadata()));
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://outbound-spot-router",
                "spotNode.entrySpot",
                "create.entrySpot",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "spot.1.sendToSpot.spot-node.target-spot.Greeting",
                "spot.1.requestToSpot.spot-node.target-spot.Ping",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotOutboundUsesMessageTypePacketNameByDefault() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://outbound-packet-name-router");
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
                .sendToSpot(targetSpotHandle(runtime), new SpotGreeting("hello"))
                .submit();
            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(targetSpotHandle(runtime), new SpotQuestion("ping"))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://outbound-packet-name-router",
                "spotNode.entrySpot",
                "create.entrySpot",
                "spotNode.createSpot",
                "create.spot.1",
                "spot.1.setRoutingId",
                "spot.1.onDispatchEvent",
                "spot.1.sendToSpot.spot-node.target-spot.SpotGreeting",
                "spot.1.requestToSpot.spot-node.target-spot.SpotQuestion",
                "close.spot.1",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotOutboundRequestReplyIgnoresBackendPacketNamePart() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://outbound-reply-packet-router");
                node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        backendFactory.nextSpotRequestReplyParts(List.of(
            Message.from("SpotRouteReply".getBytes(StandardCharsets.UTF_8)),
            Message.from("SpotAnswer".getBytes(StandardCharsets.UTF_8)),
            jsonStringMessage("reply")));

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            assertEquals(
                "reply",
                OutboundSpot.context.outbound()
                    .requestToSpot(targetSpotHandle(runtime), new SpotQuestion("ping"))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());
        }
    }

    @Test
    void spotOutboundRequestCompletesFrameworkErrorReplyAsFrameworkException() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://outbound-error-router");
                node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        backendFactory.nextSpotRequestReplyParts(List.of(
            Message.from("ZLinkFrameworkError".getBytes(StandardCharsets.UTF_8)),
            Message.from("missing target spot".getBytes(StandardCharsets.UTF_8))));

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            CompletionException error = assertThrows(
                CompletionException.class,
                () -> OutboundSpot.context.outbound()
                    .requestToSpot(targetSpotHandle(runtime), new SpotQuestion("ping"))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());
            assertInstanceOf(ZLinkFrameworkException.class, error.getCause());
            assertTrue(error.getCause().getMessage().contains("missing target spot"));
        }
    }

    @Test
    void spotOutboundUsesConfiguredClientServerEgressChannel() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var channel = options.addClientServerChannel("egress").enableClient("inproc://ingress-server");};
        { var channel = options.addClientServerChannel("ingress").enableServer("inproc://ingress-server");
            channel.addSendHandler(NoopSendHandler.class, String.class, "Noop"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(targetSpotHandle(runtime), new Greeting("hello"))
                .submit();
            awaitCall(backendFactory, "spot.1.sendToSpot.spot-node.target-spot.Greeting");
        }

        assertTrue(
            backendFactory.calls().contains(
                "spot.1.sendToSpot.spot-node.target-spot.Greeting"),
            backendFactory.calls().toString());
    }

    @Test
    void resolvedSpotHandleUsesEntrySpotRequestPath() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var route = options.addRouteMeshChannel("egress"); route.enableServer("inproc://egress-route");
            route.enableClient("inproc://egress-peer");};
        { var route = options.addRouteMeshChannel("ingress"); route.setRoutingId(RoutingId.from("ingress-route"));
            route.enableServer("inproc://ingress-route");
            route.enableClient("inproc://ingress-peer"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");node.addSpotFactory(OutboundSpot.class); }; };
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
                    .requestToSpot(targetSpotHandle(runtime), new Ping("ping"))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());
        }

        assertTrue(
            backendFactory.calls().contains(
                "entrySpot.requestToSpot.spot-node.target-spot.Ping"),
            backendFactory.calls().toString());
    }

    @Test
    void resolvedSpotHandleUsesEntrySpotSendPath() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var route = options.addRouteMeshChannel("egress"); route.enableServer("inproc://egress-route");
            route.enableClient("inproc://egress-peer");};
        { var route = options.addRouteMeshChannel("ingress"); route.enableServer("inproc://ingress-route");
            route.enableClient("inproc://ingress-peer"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(targetSpotHandle(runtime), new Ping("hello"))
                .submit();
            awaitCall(backendFactory, "entrySpot.sendToSpot.spot-node.target-spot.Ping");
        }

        assertTrue(
            backendFactory.calls().contains(
                "entrySpot.sendToSpot.spot-node.target-spot.Ping"),
            backendFactory.calls().toString());
    }

    @Test
    void resolvedSpotHandleDoesNotExposeRouteMeshSelection() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var route = options.addRouteMeshChannel("egress-a"); route.enableServer("inproc://egress-a"); };
        { var route = options.addRouteMeshChannel("egress-b"); route.enableServer("inproc://egress-b"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");node.addSpotFactory(OutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();

            OutboundSpot.context.outbound()
                .sendToSpot(targetSpotHandle(runtime), new Ping("hello"))
                .submit();
            awaitCall(backendFactory, "entrySpot.sendToSpot.spot-node.target-spot.Ping");
        }

        assertTrue(
            backendFactory.calls().contains(
                "entrySpot.sendToSpot.spot-node.target-spot.Ping"),
            backendFactory.calls().toString());
    }

    @Test
    void ambientSpotOutboundWorksInsideSpotCallback() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var channel = options.addClientServerChannel("play-events"); channel.enableClient("inproc://play-events"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://ambient-outbound-router");
                node.addSpotFactory(AmbientOutboundSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            AmbientOutboundSpot.outbound = runtime.spotOutbound();
            runtime.spotManager()
                .create(AmbientOutboundSpot.class, RoutingId.from("game-3"))
                .toCompletableFuture()
                .join();
            awaitCall(backendFactory, "dealer.send.Greeting");
        }

        assertEquals(
            true,
            backendFactory.calls().contains("dealer.send.Greeting"));
    }

    @Test
    void spotPublisherClientPublishesThroughMeshPublisher() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enablePubSub("inproc://spot-pub");}; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotPublisherClient()
                .publish("game", "stage.events", new StageOpened("opened"))
                .metadata("trace-id", "publish")
                .submit();
            awaitCall(
                backendFactory,
                "spotNode.publish.game.stage.events.StageOpened");
            assertEquals(
                Map.of("trace-id", "publish"),
                ZLinkApplicationMetadata.decode(
                    backendFactory.lastApplicationMetadata()));
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setPubBind.inproc://spot-pub",
                "spotNode.entrySpot",
                "create.entrySpot",
                "spotNode.publish.game.stage.events.StageOpened",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void spotRouterAndPubSubManualPeersConnectThroughBackendNode() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://spot-router")
                    .setRoutingId(RoutingId.from("spot-node-1"))
                    .connectRouter("inproc://spot-router-peer");
                node.enablePubSub("inproc://spot-pub")
                    .connectPeerPub("inproc://spot-pub-peer"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRoutingId",
                "spotNode.setPublisherRoutingId",
                "spotNode.setSubscriberRoutingId",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.setPubBind.inproc://spot-pub",
                "spotNode.connectPeer.inproc://spot-router-peer",
                "spotNode.connectPeer.inproc://spot-pub-peer",
                "spotNode.entrySpot",
                "create.entrySpot",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void acceptedSpotRouteChannelManualConnectionsAttachRouterChannelPeers() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var route = options.addRouteMeshChannel("api"); route.enableServer("inproc://api-route");
            route.enableClient("inproc://api-route-peer"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://spot-router");}; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        List<String> calls = backendFactory.calls();
        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.router",
                "router.setChannelName.api",
                "router.setPeerWeight.100",
                "router.connect.inproc://api-route-peer",
                "router.bind.inproc://api-route",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRouterBind.inproc://spot-router",
                "spotNode.entrySpot",
                "create.entrySpot",
                "spotNode.createRouteBridge",
                "create.spotRouteBridge",
                "spotRouteBridge.bridge.attachRouterChannel.api",
                "spotRouteBridge.bridge.close",
                "close.router",
                "close.spotNode",
                "close.context"),
            withoutSpotRouteBridgeDrainCalls(calls));
        assertEquals(
            List.of("router.connect.inproc://api-route-peer"),
            calls.stream()
                .filter(call -> call.startsWith("router.connect."))
                .toList());
        assertFalse(calls.contains("router.setConnectRoutingId"));
        assertFalse(calls.contains("router.setProbe.true"));
        assertTrue(calls.indexOf("router.connect.inproc://api-route-peer")
            < calls.indexOf("router.bind.inproc://api-route"));
        assertTrue(calls.indexOf("spotRouteBridge.bridge.attachRouterChannel.api")
            < calls.indexOf("spotRouteBridge.bridge.close"));
    }

    private static List<String> withoutSpotRouteBridgeDrainCalls(List<String> calls) {
        return calls.stream()
            .filter(call -> !call.equals("spotRouteBridge.bridge.drain"))
            .toList();
    }

    @Test
    void entrySpotRoutingIdAppliesToBackendEntrySpotBeforeBind() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-unhandled-router");
                node.enableRouter("inproc://spot-router"); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.entrySpot",
                "create.entrySpot",
                "entrySpot.setRoutingId",
                "spotNode.setRouterBind.inproc://spot-router",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void registeredEntrySpotIsActivatedAndClosedWithBackendEntrySpot() {
        LifecycleEntrySpot.configureCount.set(0);
        LifecycleEntrySpot.initializeCount.set(0);
        LifecycleEntrySpot.closingCount.set(0);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-join-router");
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
        RuntimeTestSupport.awaitClosed(backendFactory);
        assertEquals(1, LifecycleEntrySpot.closingCount.get());
        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.entrySpot",
                "create.entrySpot",
                "entrySpot.setRoutingId",
                "spotNode.setRouterBind.inproc://spot-router",
                "entrySpot.onDispatchEvent",
                "close.entrySpot",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void entrySpotDispatchReadableDrainsUnhandledActorJoinWithRejectReply() {
        LifecycleEntrySpot.rejectJoin.set(false);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-reject-router");
                node.addEntrySpot(LifecycleEntrySpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable("player-1");
            awaitCall(backendFactory, "entrySpot.replyActorJoinPayload.player-1.1.");
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorJoin.DONT_WAIT"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.1"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoinPayload.player-1.1."));
    }

    @Test
    void entrySpotActorJoinReadableCommitsAndInvokesMemberCallback() {
        LifecycleEntrySpot.lastActorJoin.set(null);
        LifecycleEntrySpot.lastJoin.set(null);
        LifecycleEntrySpot.rejectJoin.set(false);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-actor-send-router");
                node.addEntrySpot(LifecycleEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            awaitCall(backendFactory, "entrySpot.replyActorJoin.player-1.0");
            awaitCondition(() ->
                "player-1:true".equals(LifecycleEntrySpot.lastJoin.get()));
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorJoin.DONT_WAIT"));
        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.0"));
        assertTrue(backendFactory.calls().contains(
            "entrySpot.replyActorJoinPayload.player-1.0.\"entry:join-request\""));
        assertEquals("player-1:join-request", LifecycleEntrySpot.lastActorJoin.get());
        assertEquals("player-1:true", LifecycleEntrySpot.lastJoin.get());
    }

    @Test
    void entrySpotActorJoinRejectDoesNotInvokeMemberCallback() {
        LifecycleEntrySpot.lastActorJoin.set(null);
        LifecycleEntrySpot.lastJoin.set(null);
        LifecycleEntrySpot.rejectJoin.set(true);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-actor-request-router");
                node.addEntrySpot(LifecycleEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            awaitCall(backendFactory, "entrySpot.replyActorJoin.player-1.1");
        } finally {
            LifecycleEntrySpot.rejectJoin.set(false);
        }

        assertTrue(backendFactory.calls().contains("entrySpot.replyActorJoin.player-1.1"));
        assertTrue(backendFactory.calls().contains(
            "entrySpot.replyActorJoinPayload.player-1.1.\"entry-rejected:join-request\""));
        assertEquals("player-1:join-request", LifecycleEntrySpot.lastActorJoin.get());
        assertNull(LifecycleEntrySpot.lastJoin.get());
    }

    @Test
    void entrySpotActorReadableInvokesRegisteredActorSendHandlerOnActorQueue() {
        ActorSendHandler.lastMessage.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-interface-request-router");
                node.addEntrySpot(LifecycleEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            managedActor(runtime, "player-1", "player");

            backendFactory.dispatchEntrySpotActorMessage(
                "player-1",
                "String",
                "\"mark-x\"");
            awaitCondition(() -> "player-1:mark-x".equals(ActorSendHandler.lastMessage.get()));
        }

        assertEquals("player-1:mark-x", ActorSendHandler.lastMessage.get());
    }

    @Test
    void entrySpotActorReadableInvokesRegisteredActorRequestHandlerAndRepliesBoundSession() {
        ActorRequestHandler.lastRequest.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-shared-request-router");
                node.addEntrySpot(LifecycleEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            managedActor(runtime, "player-1", "player");

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
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-interface-request-router");
                node.addEntrySpot(InterfaceEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.newFrameworkRuntime(
                 options,
                 backendFactory,
                 new InterfaceActorSerializer(),
                 ZLinkHandlerActivator.reflection())) {
            managedActor(runtime, "player-1", "player");

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
    void entrySpotActorRequestUsesEntryHandlerWhenUserSpotSharesPacketName() {
        SharedEntryActorRequestHandler.lastRequest.set(null);
        SharedUserActorRequestHandler.lastRequest.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-shared-request-router");
                node.addEntrySpot(SharedEntrySpot.class); node.addSpotFactory(SharedUserSpot.class);
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            managedActor(runtime, "player-1", "player");

            backendFactory.dispatchEntrySpotActorStreamRequest(
                "player-1",
                "SharedActorRequest",
                "\"entry-request\"",
                42);
            awaitCall(backendFactory, "spotNode.sendActorBoundSession.player-1.");
            awaitCondition(() ->
                "entry:player-1:entry-request".equals(SharedEntryActorRequestHandler.lastRequest.get()));
        }

        assertEquals("entry:player-1:entry-request", SharedEntryActorRequestHandler.lastRequest.get());
        assertNull(SharedUserActorRequestHandler.lastRequest.get());
    }

    @Test
    void userSpotActorRequestCanLeaveActorAndReplyBoundSession() {
        LeaveDuringRequestSpot.lastLeave.set(null);
        LeaveDuringRequestHandler.lastRequest.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://leave-during-request-router");
                node.addSpotFactory(LeaveDuringRequestSpot.class);
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(LeaveDuringRequestSpot.class, RoutingId.from("leave-spot"))
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            actor.context()
                .joinSpot(RoutingId.from("leave-spot"), "join")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            awaitCall(backendFactory, "spot.1.replyActorJoin.player-1.");
            backendFactory.dispatchSpotActorStreamRequest(
                "player-1",
                "LeaveDuringRequest",
                "\"leave-now\"",
                42);

            awaitCall(backendFactory, "spotNode.leaveActor.player-1.");
            backendFactory.dispatchSpotActorLifecycleLeft("player-1");
            awaitCall(backendFactory, "spotNode.sendActorBoundSession.player-1.");
        }

        assertEquals("player-1:leave-now", LeaveDuringRequestHandler.lastRequest.get());
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("spotNode.leaveActor.player-1.")),
            () -> "calls: " + backendFactory.calls());
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("spotNode.sendActorBoundSession.player-1.")),
            () -> "calls: " + backendFactory.calls());
    }

    @Test
    void creatingSecondUserSpotOfSameTypeDoesNotDuplicateActorPacketRegistration() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://same-type-router");
                node.addSpotFactory(SharedUserSpot.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            assertEquals(ZLinkSpotCreateState.CREATED, runtime.spotManager()
                .getOrCreate(SharedUserSpot.class, RoutingId.from("same-type-a"))
                .toCompletableFuture()
                .join()
                .state());
            assertEquals(ZLinkSpotCreateState.CREATED, runtime.spotManager()
                .getOrCreate(SharedUserSpot.class, RoutingId.from("same-type-b"))
                .toCompletableFuture()
                .join()
                .state());
        }
    }

    @Test
    void userSpotActorJoinReadableInvokesMemberJoinAndLifecycleCallbacks() {
        InterfaceUserSpot.lastJoin.set(null);
        InterfaceUserSpot.lastPostJoin.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://interface-user-router");
                node.addSpotFactory(InterfaceUserSpot.class);
                node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.newFrameworkRuntime(
                 options,
                 backendFactory,
                 new InterfaceActorSerializer(),
                 ZLinkHandlerActivator.reflection())) {
            runtime.spotManager()
                .create(InterfaceUserSpot.class, RoutingId.from("interface-spot"))
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            actor.context()
                .joinSpot(RoutingId.from("interface-spot"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            awaitCall(backendFactory, "spot.1.replyActorJoin.player-1.0");
        }

        assertEquals("player-1:join-request", InterfaceUserSpot.lastJoin.get());
        assertEquals("player-1:true", InterfaceUserSpot.lastPostJoin.get());
    }

    @Test
    void spotContextLeaveActorMarksActorLeftAndInvokesMemberCallback() {
        OutboundSpot.lastJoin.set(null);
        OutboundSpot.lastLeave.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://context-leave-router");
                node.addEntrySpot(LifecycleEntrySpot.class);
                node.addSpotFactory(OutboundSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, RoutingId.from("game-2"))
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            actor.context()
                .joinSpot(RoutingId.from("game-2"), "join-request")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            awaitCall(backendFactory, "spot.1.replyActorJoin.player-1.0");
            awaitCondition(() -> "player-1:true".equals(OutboundSpot.lastJoin.get()));

            var leaving = OutboundSpot.context.leaveActor(actor).toCompletableFuture();
            awaitCall(backendFactory, "spotNode.leaveActor.player-1.game-2");
            backendFactory.dispatchSpotActorLifecycleLeft("player-1");
            awaitCondition(() -> "player-1:false".equals(OutboundSpot.lastLeave.get()));
            awaitCall(backendFactory, "spotNode.joinActorEntrySpot.player-1.spot-node");
            backendFactory.dispatchEntrySpotActorLifecycleJoined(
                "player-1", RoutingId.from("entrySpot"));
            leaving.join();
        }

        assertEquals("player-1:false", OutboundSpot.lastLeave.get());
    }

    @Test
    void entrySpotActorLifecycleReadableDrainsLeftEventAndInvokesMemberCallback() {
        LifecycleEntrySpot.lastJoin.set(null);
        LifecycleEntrySpot.lastLeave.set(null);
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-left-router");
                node.addEntrySpot(LifecycleEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchEntrySpotActorJoinReadable(
                "player-1",
                "String",
                "join-request");
            awaitCondition(() -> "player-1:true".equals(LifecycleEntrySpot.lastJoin.get()));
            backendFactory.dispatchEntrySpotActorLifecycleLeft("player-1");
            awaitCondition(() -> "player-1:false".equals(LifecycleEntrySpot.lastLeave.get()));
        }

        assertTrue(backendFactory.calls().contains("entrySpot.recvActorLifecycle.DONT_WAIT"));
        assertEquals("player-1:false", LifecycleEntrySpot.lastLeave.get());
    }

    @Test
    void entrySpotActorLifecycleJoinedBindsActorContextToUserSpot() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        options.addHandlersFromPackageOf(SpotRuntimeFakeBackendTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-joined-router");
                node.addEntrySpot(LifecycleEntrySpot.class);
                node.addSpotFactory(OutboundSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        String spotId = RoutingId.from("game-joined");

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.spotManager()
                .create(OutboundSpot.class, spotId)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = managedActor(runtime, "player-1", "player");

            backendFactory.dispatchEntrySpotActorLifecycleJoined("player-1", spotId);

            awaitCondition(() -> Optional.of(spotId).equals(actor.context().spotId()));
            assertEquals(Optional.of(spotId), actor.context().spotId());
            assertEquals(Optional.of(spotId), actor.context().spotId());
        }
    }

    @Test
    void spotPublisherClientRejectsUnattachedChannel() {
        DefaultZLinkFrameworkOptions options = optionsWithTargetLocation();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enablePubSub("inproc://spot-pub");}; };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(
                     options,
                     new FakeZLinkBackendAdapterFactory())) {
            assertThrows(
                ZLinkConfigurationException.class,
                () -> runtime.spotPublisherClient()
                    .publish("missing", "stage.events", message("opened", "StageOpened")));
        }
    }

    public static final class GameSpot extends TestZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onInitialize() {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class PayloadSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastCreatePayload = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
            String decoded = request.decode(String.class);
            lastCreatePayload.set(decoded);
            return CompletableFuture.completedFuture(
                ZLinkSpotCreateResponse.accept(new CreatePayload("reply:" + decoded)));
        }
    }

    public record CreatePayload(String value) {
    }

    public static final class ProtobufCreateSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastCreatePayload = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
            StringValue decoded = request.decode(StringValue.class);
            lastCreatePayload.set(decoded.getValue());
            return CompletableFuture.completedFuture(
                ZLinkSpotCreateResponse.accept(StringValue.of("reply:" + decoded.getValue())));
        }
    }

    public static final class MessagePackCreateSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastCreatePayload = new AtomicReference<>();

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
            PackedCreatePayload decoded = request.decode(PackedCreatePayload.class);
            lastCreatePayload.set(decoded.value());
            return CompletableFuture.completedFuture(
                ZLinkSpotCreateResponse.accept(new PackedCreatePayload("reply:" + decoded.value())));
        }
    }

    public record PackedCreatePayload(String value) {
    }

    public static final class CreatePayloadSerializer implements ZLinkMessageSerializer {
        @Override
        public <T> ZLinkEncodedPayload serialize(T value) {
            if (value instanceof Message message) {
                return ZLinkEncodedPayload.from(message.toByteArray());
            }
            if (value instanceof CreatePayload payload) {
                return ZLinkEncodedPayload.from(("create:" + payload.value()).getBytes(StandardCharsets.UTF_8));
            }
            return ZLinkEncodedPayload.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
            String text = new String(payload.bytes(), StandardCharsets.UTF_8);
            if (type == String.class) {
                return type.cast(text.startsWith("create:") ? text.substring("create:".length()) : text);
            }
            if (type == CreatePayload.class) {
                return type.cast(new CreatePayload(text.startsWith("create:") ? text.substring("create:".length()) : text));
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
        }
    }

    private static ZLinkMessageSerializer serializerWith(
        systems.zlink.framework.configuration.ZLinkCodecExtension extension) {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.use(extension);
        return registration.serializerWithFallback(new ZLinkJsonMessageSerializer());
    }

    private static Message messageFrom(ZLinkEncodedPayload payload) {
        return Message.from(payload.bytes());
    }

    private static Message jsonStringMessage(String value) {
        return Message.from(("\"" + value + "\"").getBytes(StandardCharsets.UTF_8));
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
        throw new AssertionError("missing call prefix " + prefix + " in " + backendFactory.calls());
    }

    private static void awaitCondition(java.util.function.BooleanSupplier condition) {
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(2).toNanos();
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new AssertionError("condition was not satisfied");
    }

    private static void awaitLatch(CountDownLatch latch) {
        try {
            if (!latch.await(1, TimeUnit.SECONDS)) {
                throw new AssertionError("latch did not open");
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new AssertionError("latch wait interrupted", error);
        }
    }

    private static void awaitFuture(CompletableFuture<Void> future, List<String> calls) {
        try {
            future.get(1, TimeUnit.SECONDS);
        } catch (Exception ex) {
            throw new AssertionError(calls.toString(), ex);
        }
    }

    public static final class OutboundSpot extends TestZLinkSpot<ZLinkActor> {
        static OutboundSpot instance;
        static ZLinkSpotContext context;
        static final AtomicReference<String> lastJoin = new AtomicReference<>();
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
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept("joined"));
        }

        @Override
        public CompletionStage<Void> onJoinedActor(
            ZLinkActor actor) {
            lastJoin.set(actor.actorId() + ":" + actor.context().spotId().isPresent());
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(
            ZLinkActor actor) {
            lastLeave.set(actor.actorId() + ":" + actor.context().spotId().isPresent());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class HandlerSpot extends TestZLinkSpot<ZLinkActor> {
        static final List<String> dispatches = new CopyOnWriteArrayList<>();
        static final Map<String, Map<String, String>> metadata =
            new java.util.concurrent.ConcurrentHashMap<>();
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
            context.handlers().addHandler(SpotCommandHandler.class);
            context.handlers().addHandler(SpotQueryHandler.class);
            context.handlers().addHandler(SpotEventHandler.class);
        }
    }

    public static final class SpotCommandHandler
        implements ZLinkSpotPacketHandler<HandlerSpot, String> {
        @Override
        public CompletionStage<Void> handle(HandlerSpot spot, String message) {
            HandlerSpot.dispatches.add("packet:" + message);
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> handle(
            HandlerSpot spot,
            String message,
            ZLinkSendContext context) {
            HandlerSpot.metadata.put("packet", context.metadata());
            return handle(spot, message);
        }
    }

    public static final class SpotQueryHandler {
        @ZLinkSpotRequest(packetName = "SpotQuery")
        public CompletionStage<String> handle(
            HandlerSpot spot,
            String request,
            ZLinkRequestContext context) {
            HandlerSpot.dispatches.add("request:" + request);
            HandlerSpot.metadata.put("request", context.metadata());
            return CompletableFuture.completedFuture("reply:" + request);
        }
    }

    @ZLinkSpotSubscription(topic = "stage.events")
    public static final class SpotEventHandler
        implements ZLinkSpotSubscriptionHandler<HandlerSpot, String> {
        @Override
        public CompletionStage<Void> handle(HandlerSpot spot, String message) {
            HandlerSpot.dispatches.add("subscription:" + message);
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> handle(
            HandlerSpot spot,
            String message,
            ZLinkPublishContext context) {
            HandlerSpot.metadata.put("subscription", context.metadata());
            return handle(spot, message);
        }
    }

    public static final class SerialSpot extends TestZLinkSpot<ZLinkActor> {
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
            context.handlers().addHandler(SerialSpotHandler.class);
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            SerialJoinRequest decoded = request.decode(SerialJoinRequest.class);
            SerialActorJoinHandler.events.add(
                "join:start:" + actorId + ":" + decoded.value());
            SerialActorJoinHandler.started.complete(null);
            SerialActorJoinHandler.release.join();
            SerialActorJoinHandler.events.add("join:end:" + actorId);
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept("joined:" + decoded.value()));
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
        public CompletionStage<Void> handle(SerialSpot spot, String message) {
            events.add("start:" + message);
            if ("first".equals(message)) {
                firstStarted.complete(null);
                return releaseFirst.thenRun(() -> events.add("end:first"));
            }
            events.add("end:" + message);
            secondStarted.complete(null);
            return CompletableFuture.completedFuture(null);
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
        public <T> ZLinkEncodedPayload serialize(T value) {
            if (value instanceof Message message) {
                return ZLinkEncodedPayload.from(message.toByteArray());
            }
            if (value instanceof byte[] bytes) {
                return ZLinkEncodedPayload.from(bytes);
            }
            if (value instanceof SerialJoinRequest request) {
                return ZLinkEncodedPayload.from(request.value().getBytes(StandardCharsets.UTF_8));
            }
            return ZLinkEncodedPayload.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
            if (type == Message.class) {
                return type.cast(Message.from(payload.bytes()));
            }
            if (type == byte[].class) {
                return type.cast(payload.bytes());
            }
            if (type == String.class) {
                return type.cast(new String(payload.bytes(), StandardCharsets.UTF_8));
            }
            if (type == SerialJoinRequest.class) {
                return type.cast(new SerialJoinRequest(new String(payload.bytes(), StandardCharsets.UTF_8)));
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
        }
    }

    public static final class NoopSendHandler
        implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handle(
            String message,
            ZLinkSendContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class AmbientOutboundSpot extends TestZLinkSpot<ZLinkActor> {
        static ZLinkSpotOutbound outbound;

        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override
        public CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
            outbound
                .sendToChannel("play-events", new Greeting("hello"))
                .submit();
            return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
        }
    }

    public static final class LifecycleEntrySpot implements ZLinkEntrySpot<ZLinkActor> {
        static final AtomicInteger configureCount = new AtomicInteger();
        static final AtomicInteger initializeCount = new AtomicInteger();
        static final AtomicInteger closingCount = new AtomicInteger();
        static final AtomicReference<String> lastActorJoin = new AtomicReference<>();
        static final AtomicReference<String> lastJoin = new AtomicReference<>();
        static final AtomicReference<String> lastLeave = new AtomicReference<>();
        static final AtomicReference<Boolean> rejectJoin = new AtomicReference<>(false);
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
        public CompletionStage<Void> onInitialize() {
            initializeCount.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onClosing() {
            closingCount.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            String decoded = request.decode(String.class);
            lastActorJoin.set(actorId + ":" + decoded);
            if (Boolean.TRUE.equals(rejectJoin.get())) {
                return CompletableFuture.completedFuture(
                    ZLinkSpotActorJoinResponse.reject("entry-rejected:" + decoded));
            }
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept("entry:" + decoded));
        }

        @Override
        public CompletionStage<Void> onJoinedActor(
            ZLinkActor actor) {
            lastJoin.set(actor.actorId() + ":" + actor.context().spotId().isPresent());
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(
            ZLinkActor actor) {
            lastLeave.set(actor.actorId() + ":" + actor.context().spotId().isPresent());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class InterfaceEntrySpot extends TestZLinkEntrySpot<ZLinkActor> {
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
        public CompletionStage<String> handle(
            InterfaceEntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            InterfaceActorRequest request) {
            lastRequest.set(actor.actorId() + ":" + request.value());
            return CompletableFuture.completedFuture("reply:" + request.value());
        }
    }

    public static final class SharedEntrySpot extends TestZLinkEntrySpot<ZLinkActor> {
        private final ZLinkEntrySpotContext context;

        public SharedEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(SharedEntryActorRequestHandler.class);
        }
    }

    public static final class SharedUserSpot extends TestZLinkSpot<ZLinkActor> {
        private final ZLinkSpotContext context;

        public SharedUserSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(SharedUserActorRequestHandler.class);
        }
    }

    public static final class SharedEntryActorRequestHandler {
        static final AtomicReference<String> lastRequest = new AtomicReference<>();

        @ZLinkSpotActorRequest(packetName = "SharedActorRequest")
        public CompletionStage<String> handle(
            SharedEntrySpot spot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            String request) {
            lastRequest.set("entry:" + actor.actorId() + ":" + request);
            return CompletableFuture.completedFuture("entry-reply:" + request);
        }
    }

    public static final class SharedUserActorRequestHandler {
        static final AtomicReference<String> lastRequest = new AtomicReference<>();

        @ZLinkSpotActorRequest(packetName = "SharedActorRequest")
        public CompletionStage<String> handle(
            SharedUserSpot spot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            String request) {
            lastRequest.set("user:" + actor.actorId() + ":" + request);
            return CompletableFuture.completedFuture("user-reply:" + request);
        }
    }

    public static final class LeaveDuringRequestSpot extends TestZLinkSpot<ZLinkActor> {
        static final AtomicReference<String> lastLeave = new AtomicReference<>();
        private final ZLinkSpotContext context;

        public LeaveDuringRequestSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(LeaveDuringRequestHandler.class);
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            return CompletableFuture.completedFuture(
                ZLinkSpotActorJoinResponse.accept("joined"));
        }

        @Override
        public CompletionStage<Void> onLeaveActor(
            ZLinkActor actor) {
            lastLeave.set(actor.actorId() + ":" + actor.context().spotId().isPresent());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class LeaveDuringRequestHandler {
        static final AtomicReference<String> lastRequest = new AtomicReference<>();

        @ZLinkSpotActorRequest(packetName = "LeaveDuringRequest")
        public CompletionStage<String> handle(
            LeaveDuringRequestSpot spot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            String request) {
            lastRequest.set(actor.actorId() + ":" + request);
            return spot.context().leaveActor(actor).thenApply(
                ignored -> "left:" + request);
        }
    }

    public static final class InterfaceUserSpot extends TestZLinkSpot<ZLinkActor> {
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
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            String decoded = request.decode(String.class);
            lastJoin.set(actorId + ":" + decoded);
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept("joined:" + decoded));
        }

        @Override
        public CompletionStage<Void> onJoinedActor(
            ZLinkActor actor) {
            lastPostJoin.set(actor.actorId() + ":" + actor.context().spotId().isPresent());
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkPacket("InterfaceActorRequest")
    public record InterfaceActorRequest(String value) {
    }

    public static final class InterfaceActorSerializer implements ZLinkMessageSerializer {
        @Override
        public <T> ZLinkEncodedPayload serialize(T value) {
            if (value instanceof Message message) {
                return ZLinkEncodedPayload.from(message.toByteArray());
            }
            if (value instanceof InterfaceActorRequest request) {
                return ZLinkEncodedPayload.from(request.value().getBytes(StandardCharsets.UTF_8));
            }
            return ZLinkEncodedPayload.from(String.valueOf(value).getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
            if (type == Message.class) {
                return type.cast(Message.from(payload.bytes()));
            }
            if (type == String.class) {
                return type.cast(new String(payload.bytes(), StandardCharsets.UTF_8));
            }
            if (type == InterfaceActorRequest.class) {
                return type.cast(new InterfaceActorRequest(new String(payload.bytes(), StandardCharsets.UTF_8)));
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
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
        public CompletionStage<ZLinkActor> create(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public record SpotGreeting(String value) {
    }

    @ZLinkPacket("Greeting")
    public record Greeting(String value) {
    }

    @ZLinkPacket("Ping")
    public record Ping(String value) {
    }

    @ZLinkPacket("StageOpened")
    public record StageOpened(String value) {
    }

    @ZLinkPacket("SpotQuestion")
    public record SpotQuestion(String value) {
    }

    private static String message(String value, String packetName) {
        return value;
    }

    private static byte[] metadataFrame(String key, String value) {
        return ZLinkApplicationMetadata.copyOf(Map.of(key, value)).encode();
    }

    private static SpotHandle targetSpotHandle(ZLinkFrameworkRuntime runtime) {
        return runtime.spotHandleResolver()
            .resolveSpotHandle(RoutingId.from("target-spot"))
            .toCompletableFuture()
            .join()
            .orElseThrow();
    }

    private static DefaultZLinkFrameworkOptions optionsWithTargetLocation() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        Instant now = Instant.now();
        store.renewOwnerLease("target-owner", RoutingId.from("spot-node"), Duration.ofHours(1))
            .toCompletableFuture().join();
        store.updateSpot(new ZLinkSpotLocation(
            "game", RoutingId.from("target-spot"), "target", RoutingId.from("spot-node"),
            ZLinkSpotKind.USER, "inproc://spot-router", "target-owner", 1, now),
            ZLinkLocationWriteIntent.TAKEOVER).toCompletableFuture().join();
        options.addLocationStore(store);
        return options;
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
}
