package systems.zlink.framework.runtime.channels;

import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.lang.reflect.Method;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.ExecutionException;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkSpotAddress;
import systems.zlink.framework.runtime.backend.*;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;

final class ZLinkChannelRuntimeTest {
    @Test
    void methodArgumentsBindMessageContextAndCancellationTokenLikeDotnet() throws Exception {
        DefaultRequestContext context = new DefaultRequestContext();
        Method method = ContextHandler.class.getMethod(
            "handle",
            String.class,
            ZLinkRequestContext.class,
            CancellationToken.class);

        Object[] arguments = ZLinkChannelRuntime.methodArguments(method, "hello", context);

        assertSame("hello", arguments[0]);
        assertSame(context, arguments[1]);
        assertSame(context.cancellationToken(), arguments[2]);
    }

    @Test
    void routeBridgeRawRequestCompletesWhenRouteLoopReceivesRawReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(Message.from("raw-request".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("{\"ok\":true,\"response\":{\"value\":\"reply\"}}".getBytes()))));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"ok\":true,\"response\":{\"value\":\"reply\"}}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertFalse(backend.bridge.nativeCallbackInvoked);
        }
    }

    @Test
    void routeClientSpotRequestUsesRouteBridge() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.requestReplyParts = List.of(Message.from(
            "{\"ok\":true,\"response\":{\"value\":\"reply\"}}".getBytes()));
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);

            TestReply reply = runtime.requestToSpot(
                    "play.route",
                    new ZLinkSpotAddress(
                        "play",
                        RoutingId.from("play-node"),
                        RoutingId.from("room-spot")),
                    new TestRequest("hello"))
                .packetName("TestRequest")
                .timeout(Duration.ofMillis(300))
                .await(TestReply.class);

            assertEquals("reply", reply.value());
            assertEquals("play.route", backend.bridge.lastChannelName);
            assertEquals(RoutingId.from("play-node"), backend.bridge.lastTargetNodeRid);
            assertEquals(RoutingId.from("room-spot"), backend.bridge.lastTargetSpotRid);
            assertEquals("TestRequest", backend.bridge.lastParts.get(0).toUtf8String());
        }
    }

    @Test
    void routeClientSpotSendUsesRouteBridge() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);

            runtime.sendToSpot(
                    "play.route",
                    new ZLinkSpotAddress(
                        "play",
                        RoutingId.from("play-node"),
                        RoutingId.from("room-spot")),
                    new TestRequest("hello"))
                .packetName("TestCommand")
                .await();

            assertEquals("play.route", backend.bridge.lastChannelName);
            assertEquals(RoutingId.from("play-node"), backend.bridge.lastTargetNodeRid);
            assertEquals(RoutingId.from("room-spot"), backend.bridge.lastTargetSpotRid);
            assertEquals("TestCommand", backend.bridge.lastParts.get(0).toUtf8String());
        }
    }

    @Test
    void routeBridgeRawRequestConsumesNativeReplyEvenWhenRequestSeqIsPresent() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(Message.from("raw-request".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(7L),
                List.of(Message.from("{\"ok\":true,\"response\":{\"value\":\"reply\"}}".getBytes()))));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"ok\":true,\"response\":{\"value\":\"reply\"}}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertFalse(backend.bridge.routerReceivedInvoked);
            assertEquals(0, backend.router.replyCount);
        }
    }

    @Test
    void routeBridgeRawRequestUnwrapsRoutedSpotEnvelopeReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(
                    Message.from("StateRequest".getBytes()),
                    Message.from("{\"op\":\"ping\"}".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(7L),
                List.of(
                    Message.from("__zlink.routed_spot.egress.request".getBytes()),
                    Message.from("StateReply".getBytes()),
                    Message.from("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}".getBytes()))));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertFalse(backend.bridge.routerReceivedInvoked);
            assertEquals(0, backend.router.replyCount);
        }
    }

    @Test
    void routeBridgeNativeCallbackRequestUnwrapsRoutedSpotEnvelopeReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.requestReplyParts = List.of(
            Message.from("__zlink.routed_spot.egress.request".getBytes()),
            Message.from("StateReply".getBytes()),
            Message.from("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}".getBytes()));
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(
                    Message.from("StateRequest".getBytes()),
                    Message.from("{\"op\":\"ping\"}".getBytes())),
                Duration.ofMillis(300));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    void routeBridgeNativeCallbackRequestStripsSpotPacketNameReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.requestReplyParts = List.of(
            Message.from("StateReply".getBytes()),
            Message.from("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}".getBytes()));
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(
                    Message.from("StateRequest".getBytes()),
                    Message.from("{\"op\":\"ping\"}".getBytes())),
                Duration.ofMillis(300));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    void routeBridgeRequestIgnoresNativeRequestEchoAndWaitsForRouterReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.requestReplyParts = List.of(
            Message.from("__zlink.routed_spot.egress.request".getBytes()),
            Message.from("StateRequest".getBytes()));
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(
                    Message.from("StateRequest".getBytes()),
                    Message.from("{\"op\":\"ping\"}".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(7L),
                List.of(
                    Message.from("__zlink.routed_spot.egress.request".getBytes()),
                    Message.from("StateReply".getBytes()),
                    Message.from("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}".getBytes()))));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    void routeBridgeRequestIgnoresRouterRequestEchoAndWaitsForRouterReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(
                    Message.from("StateRequest".getBytes()),
                    Message.from("{\"op\":\"ping\"}".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(6L),
                List.of(
                    Message.from("__zlink.routed_spot.egress.request".getBytes()),
                    Message.from("StateRequest".getBytes()))));
            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(7L),
                List.of(
                    Message.from("__zlink.routed_spot.egress.request".getBytes()),
                    Message.from("StateReply".getBytes()),
                    Message.from("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}".getBytes()))));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"spotRid\":\"room-spot\",\"value\":\"pong\"}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    void routeBridgeLateRawReplyAfterNativeCompletionIsNotRepliedAsMissingRouteHandler()
        throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            runtime.requestToSpotViaRouterChannel(
                    "play.route",
                    RoutingId.from("play-node"),
                    RoutingId.from("room-spot"),
                    List.of(Message.from("raw-request".getBytes())),
                    Duration.ofMillis(300))
                .toCompletableFuture()
                .get(1, TimeUnit.SECONDS);

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(7L),
                List.of(
                    Message.from("StateRequest".getBytes()),
                    Message.from("{\"op\":\"late\"}".getBytes()))));
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(1);
            while (!backend.router.inbound.isEmpty() && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }

            assertTrue(backend.router.inbound.isEmpty());
            Thread.sleep(50);
            assertEquals(0, backend.router.replyCount);
        }
    }

    @Test
    void routeBridgeRawRequestCompletesBeforeBridgeConsumesActorJoinReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        backend.bridge.consumeRouterReceived = true;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(Message.from("raw-request".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("true\nactor-node\nplayer-x\n1\ncmVwbHk=".getBytes()))));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("true\nactor-node\nplayer-x\n1\ncmVwbHk=", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertFalse(backend.bridge.routerReceivedInvoked);
        }
    }

    @Test
    void routeBridgeRawRequestFailsOnFrameworkErrorReplyWithoutBridgeFeedback() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        backend.bridge.consumeRouterReceived = true;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);
            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(Message.from("raw-request".getBytes())),
                Duration.ofMillis(300));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.empty(),
                List.of(
                    Message.from("ZLinkFrameworkError".getBytes()),
                    Message.from("missing route handler".getBytes()))));

            ExecutionException error = org.junit.jupiter.api.Assertions.assertThrows(
                ExecutionException.class,
                () -> request.toCompletableFuture().get(1, TimeUnit.SECONDS));
            assertInstanceOf(ZLinkFrameworkException.class, error.getCause());
            assertEquals("missing route handler", error.getCause().getMessage());
            assertFalse(backend.bridge.routerReceivedInvoked);
        }
    }

    @Test
    void routeMeshFrameworkErrorRequestIsDroppedWithoutErrorReply() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.of(7L),
                List.of(
                    Message.from("ZLinkFrameworkError".getBytes()),
                    Message.from("missing route handler".getBytes()))));

            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(1);
            while (!backend.router.inbound.isEmpty() && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }

            assertTrue(backend.router.inbound.isEmpty());
            Thread.sleep(50);
            assertEquals(0, backend.router.replyCount);
            assertFalse(backend.bridge.routerReceivedInvoked);
        }
    }

    @Test
    void routeBridgeDrainFailureDoesNotStopLaterRawReplyCompletion() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        backend.bridge.drainFailuresRemaining = 1;
        backend.bridge.firstDrainFailed = new CountDownLatch(1);
        backend.bridge.nextDrainAfterFailure = new CountDownLatch(1);
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);

            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(Message.from("raw-request".getBytes())),
                Duration.ofMillis(300));
            assertTrue(backend.bridge.firstDrainFailed.await(1, TimeUnit.SECONDS));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("{\"ok\":true}".getBytes()))));
            assertTrue(backend.bridge.nextDrainAfterFailure.await(1, TimeUnit.SECONDS));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"ok\":true}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertEquals(0, backend.bridge.drainFailuresRemaining);
        }
    }

    @Test
    void routeBridgeNoDataDrainDoesNotStopLaterRawReplyCompletion() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addRouteMeshChannel("play.route")
            .enableServer("inproc://play-route");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.bridge.completeRequests = false;
        backend.bridge.noDataDrainsRemaining = 1;
        backend.bridge.firstDrainFailed = new CountDownLatch(1);
        backend.bridge.nextDrainAfterFailure = new CountDownLatch(1);
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            runtime.registerSpotRouteBridgeOwner(() -> backend.spotNode);

            var request = runtime.requestToSpotViaRouterChannel(
                "play.route",
                RoutingId.from("play-node"),
                RoutingId.from("room-spot"),
                List.of(Message.from("raw-request".getBytes())),
                Duration.ofMillis(300));
            assertTrue(backend.bridge.firstDrainFailed.await(1, TimeUnit.SECONDS));

            backend.router.inbound.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("play-node")),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("{\"ok\":true}".getBytes()))));
            assertTrue(backend.bridge.nextDrainAfterFailure.await(1, TimeUnit.SECONDS));

            List<Message> reply = request.toCompletableFuture().get(1, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("{\"ok\":true}", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertEquals(0, backend.bridge.noDataDrainsRemaining);
        }
    }

    @Test
    void clientServerRequestCompletesExceptionallyWhenBackendResultIsNotOk() throws Exception {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(300));
        options.addClientServerChannel("api")
            .enableClient("inproc://api");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        backend.dealer.requestResult = ZLinkBackendRequestResult.TIMED_OUT;
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            var request = runtime.requestToChannel("api", "payload")
                .timeout(Duration.ofMillis(300))
                .submit(String.class);

            ExecutionException error = org.junit.jupiter.api.Assertions.assertThrows(
                ExecutionException.class,
                () -> request.toCompletableFuture().get(1, TimeUnit.SECONDS));
            assertInstanceOf(ZLinkFrameworkException.class, error.getCause());
        }
    }

    @Test
    void clientServerRuntimeOptionsReadAndWriteServerWeight() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("api")
            .enableServer("inproc://api");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            var socket = runtime.clientServerChannel("api").configureServerSocket();

            assertEquals(100, socket.weight());
            socket.weight(0);
            assertEquals(0, socket.weight());
            socket.weight(100);
            assertEquals(100, socket.weight());
        }
    }

    @Test
    void clientServerRuntimeOptionsRejectInvalidServerWeight() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("api")
            .enableServer("inproc://api");
        FakeChannelBackendAdapter backend = new FakeChannelBackendAdapter();
        try (ZLinkChannelRuntime runtime = new ZLinkChannelRuntime(
            backend,
            options.registration(),
            new ZLinkJsonMessageSerializer())) {
            var socket = runtime.clientServerChannel("api").configureServerSocket();

            org.junit.jupiter.api.Assertions.assertThrows(
                ZLinkConfigurationException.class,
                () -> socket.weight(-1));
            org.junit.jupiter.api.Assertions.assertThrows(
                ZLinkConfigurationException.class,
                () -> socket.weight(101));
        }
    }

    public static final class ContextHandler {
        public void handle(
            String request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken) {
        }
    }

    private static final class DefaultRequestContext implements ZLinkRequestContext {
        private static final CancellationToken NONE = () -> false;

        @Override
        public java.util.Optional<String> channelName() {
            return java.util.Optional.of("profile");
        }

        @Override
        public java.util.Optional<String> packetName() {
            return java.util.Optional.of("Echo");
        }

        @Override
        public java.util.Optional<String> contentType() {
            return java.util.Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private static final class FakeChannelBackendAdapter implements ZLinkChannelBackendAdapter {
        final FakeContext context = new FakeContext();
        final FakeDealerSocket dealer = new FakeDealerSocket();
        final FakeRouterSocket router = new FakeRouterSocket();
        final FakeSpotRouteBridge bridge = new FakeSpotRouteBridge();
        final FakeSpotNode spotNode = new FakeSpotNode(bridge);

        @Override
        public ZLinkBackendContext createContext() {
            return context;
        }

        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            return dealer;
        }

        @Override
        public ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context) {
            return router;
        }

        @Override
        public ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendSubscriberSocket createSubscriberSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }
    }

    private static final class FakeDealerSocket implements ZLinkBackendDealerSocket {
        ZLinkBackendRequestResult requestResult = ZLinkBackendRequestResult.OK;

        @Override public void setChannelName(String channelName) { }
        @Override public void bind(String endpoint) { }
        @Override public void connect(String endpoint) { }
        @Override public void disconnect(String endpoint) { }
        @Override public boolean send(List<Message> parts, SendFlags flags) { return true; }
        @Override public boolean request(List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            callback.handle(new ZLinkBackendReceived(
                requestResult,
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                List.of()));
            return true;
        }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) { return null; }
        @Override public String name() { return "fake-dealer"; }
        @Override public void close() { }
    }

    private static final class FakeContext implements ZLinkBackendContext {
        @Override
        public void shutdown() {
        }

        @Override
        public String name() {
            return "fake-context";
        }

        @Override
        public void close() {
        }
    }

    private static final class FakeRouterSocket implements ZLinkBackendRouterSocket {
        final ArrayDeque<ZLinkBackendReceived> inbound = new ArrayDeque<>();
        int peerWeight = 100;
        int replyCount;
        RoutingId connectRoutingId;

        @Override public void setChannelName(String channelName) { }
        @Override public void setRoutingId(RoutingId routingId) { }
        @Override public void setConnectRoutingId(RoutingId routingId) { connectRoutingId = routingId; }
        @Override public int peerWeight() { return peerWeight; }
        @Override public void setPeerWeight(int weight) { peerWeight = weight; }
        @Override public void bind(String endpoint) { }
        @Override public void connect(String endpoint) { }
        @Override public void disconnect(String endpoint) { }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) { return inbound.poll(); }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) { return true; }
        @Override public boolean request(RoutingId routingId, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) { return true; }
        @Override public void reply(RoutingId routingId, long requestSeq, List<Message> parts) { replyCount++; }
        @Override public String name() { return "fake-router"; }
        @Override public void close() { }
    }

    private static final class FakeSpotRouteBridge implements ZLinkBackendSpotRouteBridge {
        boolean nativeCallbackInvoked;
        boolean routerReceivedInvoked;
        int drains;
        boolean completeRequests = true;
        boolean consumeRouterReceived;
        ZLinkBackendRequestResult requestResult = ZLinkBackendRequestResult.OK;
        List<Message> requestReplyParts = List.of();
        String lastChannelName;
        RoutingId lastTargetNodeRid;
        RoutingId lastTargetSpotRid;
        List<Message> lastParts = List.of();
        int drainFailuresRemaining;
        int noDataDrainsRemaining;
        CountDownLatch firstDrainFailed;
        CountDownLatch nextDrainAfterFailure;

        @Override public void attachRouterChannel(String channelName, ZLinkBackendRouterSocket router) { }
        @Override public boolean send(String channelName, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, SendFlags flags) {
            recordBridgeCall(channelName, targetNodeRid, targetSpotRid, parts);
            return true;
        }
        @Override public boolean request(String channelName, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            recordBridgeCall(channelName, targetNodeRid, targetSpotRid, parts);
            if (!completeRequests) {
                return true;
            }
            callback.handle(new ZLinkBackendReceived(
                requestResult,
                Optional.empty(),
                Optional.of(targetSpotRid),
                Optional.empty(),
                requestReplyParts));
            return true;
        }
        private void recordBridgeCall(
            String channelName,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            List<Message> parts) {
            lastChannelName = channelName;
            lastTargetNodeRid = targetNodeRid;
            lastTargetSpotRid = targetSpotRid;
            lastParts.forEach(Message::close);
            lastParts = parts.stream()
                .map(Message::toByteArray)
                .map(Message::from)
                .toList();
        }
        @Override public boolean handleRouterReceived(String channelName, RoutingId sourceNodeRid, long requestSeq, List<Message> parts) {
            routerReceivedInvoked = true;
            return consumeRouterReceived;
        }
        @Override public int drain() {
            drains++;
            if (noDataDrainsRemaining > 0) {
                noDataDrainsRemaining--;
                if (firstDrainFailed != null) {
                    firstDrainFailed.countDown();
                }
                throw new ZlinkRecvException(RecvResult.NO_DATA);
            }
            if (drainFailuresRemaining > 0) {
                drainFailuresRemaining--;
                if (firstDrainFailed != null) {
                    firstDrainFailed.countDown();
                }
                throw new IllegalStateException("synthetic drain failure");
            }
            if (firstDrainFailed != null
                && firstDrainFailed.getCount() == 0
                && nextDrainAfterFailure != null) {
                nextDrainAfterFailure.countDown();
            }
            return drains;
        }
        @Override public String name() { return "fake-bridge"; }
        @Override public void close() { lastParts.forEach(Message::close); }
    }

    private record TestRequest(String value) {
    }

    private record TestReply(String value) {
    }

    private static final class FakeSpotNode implements ZLinkBackendSpotNode {
        private final FakeSpotRouteBridge bridge;

        FakeSpotNode(FakeSpotRouteBridge bridge) {
            this.bridge = bridge;
        }

        @Override public RoutingId routingId() { return RoutingId.from("owner-node"); }
        @Override public void setRoutingId(RoutingId routingId) { }
        @Override public void setPublisherRoutingId(RoutingId routingId) { }
        @Override public void setSubscriberRoutingId(RoutingId routingId) { }
        @Override public void setRouterBind(String endpoint) { }
        @Override public void setPubBind(String endpoint) { }
        @Override public void connectPeer(String endpoint) { }
        @Override public void connectPeer(RoutingId peerRid, String endpoint) { }
        @Override public void disconnectPeer(String endpoint) { }
        @Override public void disconnectPeer(RoutingId peerRid) { }
        @Override public ZLinkBackendSpotRouteBridge createRouteBridge() { return bridge; }
        @Override public ZLinkBackendSpot createSpot() { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendSpot entrySpot() { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendActorRef createActor(String actorId, Message createRequest) { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendActorRef actorLookup(String actorId) { throw new UnsupportedOperationException(); }
        @Override public java.util.concurrent.CompletionStage<ZLinkBackendActorJoinResult> joinActor(ZLinkBackendActorRef actor, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public java.util.concurrent.CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(ZLinkBackendActorRef actor, RoutingId targetNodeRid, Message request, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public java.util.concurrent.CompletionStage<List<Message>> leaveActor(ZLinkBackendActorRef actor, RoutingId currentSpotRid, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public java.util.concurrent.CompletionStage<Void> destroyActor(ZLinkBackendActorRef actor, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) { return false; }
        @Override public void replyActorNoBind(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid, long requestId, int flags, List<Message> parts) { throw new UnsupportedOperationException(); }
        @Override public boolean sendToActor(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) { return false; }
        @Override public java.util.concurrent.CompletionStage<List<Message>> requestToActor(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public boolean forwardActorBoundSession(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid, List<Message> parts, SendFlags flags) { return false; }
        @Override public void bindRemoteActorBoundSession(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid) { }
        @Override public void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout) { }
        @Override public SpotNodeStatus status() { throw new UnsupportedOperationException(); }
        @Override public List<SpotNodePeerEntry> peers() { return List.of(); }
        @Override public List<SpotNodeSubjectEntry> subjects() { return List.of(); }
        @Override public String name() { return "fake-node"; }
        @Override public void close() { }
    }
}
