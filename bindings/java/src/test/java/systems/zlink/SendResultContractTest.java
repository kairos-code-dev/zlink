package systems.zlink;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointOptions;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.contracts.service.spot.SpotNodePublisher;
import systems.zlink.contracts.service.spot.SpotRouteBridge;
import systems.zlink.contracts.sockets.SubSocket;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.XPubSocket;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SendResultContractTest {
    @Test
    public void subscribeDontWaitReturnsNoDataWhenNoTopicDeliveryExists() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SubSocket sub = ctx.createSubSocket()) {
            assertFalse(sub.subscribe(new TopicMessage(), RecvFlags.DONT_WAIT));
        }
    }

    @Test
    public void spotSubscribeDontWaitReturnsNoDataWhenNoTopicDeliveryExists() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot()) {
            assertFalse(spot.subscribe(new TopicMessage(), RecvFlags.DONT_WAIT));
        }
    }

    @Test
    public void receiveSubscriptionEventDontWaitReturnsNoDataWhenNoEventExists() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             XPubSocket pub = ctx.createXPubSocket()) {
            assertFalse(pub.receiveSubscriptionEvent(
                new SubscriptionEvent(), RecvFlags.DONT_WAIT));
        }
    }

    @Test
    public void sendDontWaitThrowsWhenRouterHasNoMatchingPeer() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket router = ctx.createRouterSocket();
             Message payload = Message.from("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.from(
                "router-missing-peer".getBytes(StandardCharsets.UTF_8));
            ZlinkSubmitException ex = assertThrows(ZlinkSubmitException.class,
                () -> router.send(missingRid)
                    .message(payload)
                    .flags(SendFlags.DONT_WAIT)
                    .submit());
            assertEquals(SubmitResult.NOT_CONNECTED, ex.getResult());
        }
    }

    @Test
    public void spotRouteBridgeRequestUsesCallerOwnedRouterSocket()
        throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot();
             RouterSocket bridgeRouter = ctx.createRouterSocket();
             RouterSocket router = ctx.createRouterSocket();
             SpotRouteBridge bridge = node.createRouteBridge()) {
            String endpoint = TestSupport.tcpEndpoint();
            RoutingId bridgeRid = RoutingId.from("spot-bridge-client");
            RoutingId routerRid = RoutingId.from("spot-bridge-server");
            bridgeRouter.setRoutingId(bridgeRid);
            router.setRoutingId(routerRid);
            router.bind(endpoint);
            bridgeRouter.connect(endpoint);
            bridge.attachRouterChannel("api", bridgeRouter);

            for (int i = 0; i < 2; i++) {
                String requestText = "spot-ping-" + i;
                String replyText = "spot-pong-" + i;
                CompletionStage<List<Message>> replyFuture =
                    bridge.request("api", routerRid, spot.getRoutingId())
                        .message(Message.from(requestText))
                        .timeout(Duration.ofSeconds(2))
                        .submit();

                try (Received received = new Received()) {
                    TestSupport.awaitCondition(() ->
                        router.recv(received, RecvFlags.DONT_WAIT));
                    assertEquals(requestText,
                        received.parts().get(received.parts().size() - 1)
                            .toUtf8String());
                    received.reply().message(Message.from(replyText)).submit();
                }

                List<Message> reply = replyFuture.toCompletableFuture()
                    .get(3, TimeUnit.SECONDS);
                try {
                    assertEquals(1, reply.size());
                    assertEquals(replyText, reply.get(0).toUtf8String());
                } finally {
                    reply.forEach(Message::close);
                }
            }
        }
    }

    @Test
    public void spotRouteBridgeRouterDrainCompletesRouterBridgeRequest()
        throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode targetNode = ctx.createSpotNode();
             Spot targetSpot = targetNode.createSpot();
             RouterSocket targetRouter = ctx.createRouterSocket();
            SpotRouteBridge targetBridge = targetNode.createRouteBridge()) {
            targetSpot.setRoutingId(RoutingId.from("target-spot"));
            String endpoint = TestSupport.tcpEndpoint();
            RoutingId targetRouterRid = RoutingId.from("target-router");
            targetRouter.setRoutingId(targetRouterRid);
            targetRouter.bind(endpoint);
            targetBridge.attachRouterChannel(
                "api",
                targetRouter,
                new SpotRouteBridgeEndpointOptions());

            try (SpotNode sourceNode = ctx.createSpotNode();
                 RouterSocket sourceRouter = ctx.createRouterSocket();
                 SpotRouteBridge sourceBridge = sourceNode.createRouteBridge()) {
                sourceRouter.setRoutingId(RoutingId.from("source-router"));
                sourceRouter.connect(endpoint);
                Thread.sleep(50);
                sourceBridge.attachRouterChannel("api", sourceRouter);

                for (int i = 0; i < 2; i++) {
                    String requestText = "bridge-ping-" + i;
                    String replyText = "bridge-pong-" + i;
                    CompletionStage<List<Message>> replyStage =
                        sourceBridge.request("api", targetRouterRid, targetSpot.getRoutingId())
                            .message(Message.from(requestText))
                            .timeout(Duration.ofSeconds(2))
                            .submit();

                    TestSupport.awaitCondition(() -> {
                        try (Received inbound = new Received()) {
                            if (!targetRouter.recv(inbound, RecvFlags.DONT_WAIT)) {
                                return false;
                            }
                            return targetBridge.handleRouterReceived(
                                "api",
                                inbound.getRoutingId().orElseThrow(),
                                inbound.requestSeq().orElseThrow(),
                                inbound.parts());
                        }
                    });

                    TestSupport.awaitCondition(() -> {
                        try (Received routed = new Received()) {
                            if (!targetSpot.recvRouted(routed, RecvFlags.DONT_WAIT)) {
                                return false;
                            }
                            assertEquals(requestText,
                                routed.parts().get(0).toUtf8String());
                            routed.reply()
                                .message(Message.from(replyText))
                                .submit();
                            return true;
                        }
                    });

                    TestSupport.awaitCondition(() -> targetBridge.drain() > 0);
                    List<Message> reply = replyStage.toCompletableFuture()
                        .get(3, TimeUnit.SECONDS);
                    try {
                        assertEquals(1, reply.size());
                        assertEquals(replyText, reply.get(0).toUtf8String());
                    } finally {
                        reply.forEach(Message::close);
                    }
                }
            }
        }
    }

    @Test
    public void spotRouteBridgeIngressRaisesDispatchHandlerForTargetSpot()
        throws Exception {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();
        AtomicBoolean dispatched = new AtomicBoolean();
        try (Context targetCtx = Zlink.createContext();
             SpotNode targetNode = targetCtx.createSpotNode(SpotNodeMode.ROUTED);
             Spot targetSpot = targetNode.createSpot();
             RouterSocket targetRouter = targetCtx.createRouterSocket();
             SpotRouteBridge targetBridge = targetNode.createRouteBridge();
             Context sourceCtx = Zlink.createContext();
             SpotNode sourceNode = sourceCtx.createSpotNode(SpotNodeMode.ROUTED);
             RouterSocket sourceRouter = sourceCtx.createRouterSocket();
             SpotRouteBridge sourceBridge = sourceNode.createRouteBridge()) {
            targetSpot.setRoutingId(RoutingId.from("target-spot-dispatch-bridge"));
            RoutingId targetRouterRid = RoutingId.from("target-router-dispatch-bridge");
            targetRouter.setRoutingId(targetRouterRid);
            sourceRouter.setRoutingId(RoutingId.from("source-router-dispatch-bridge"));
            targetRouter.bind(endpoint);
            targetBridge.attachRouterChannel(
                "ingress",
                targetRouter,
                new SpotRouteBridgeEndpointOptions());
            targetSpot.setDispatchHandler(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try (Received routed = new Received()) {
                    if (!targetSpot.recvRouted(routed, RecvFlags.DONT_WAIT)) {
                        return;
                    }
                    assertEquals("dispatch-bridge-ping",
                        routed.parts().get(0).toUtf8String());
                    routed.reply()
                        .message(Message.from("dispatch-bridge-pong"))
                        .submit();
                    dispatched.set(true);
                }
            });

            sourceRouter.connect(endpoint);
            Thread.sleep(50);
            sourceBridge.attachRouterChannel("egress", sourceRouter);

            CompletionStage<List<Message>> replyStage =
                sourceBridge.request("egress", targetRouterRid, targetSpot.getRoutingId())
                    .message(Message.from("dispatch-bridge-ping"))
                    .timeout(Duration.ofSeconds(2))
                    .submit();

            TestSupport.awaitCondition(() -> {
                try (Received inbound = new Received()) {
                    if (!targetRouter.recv(inbound, RecvFlags.DONT_WAIT)) {
                        return false;
                    }
                    return targetBridge.handleRouterReceived(
                        "ingress",
                        inbound.getRoutingId().orElseThrow(),
                        inbound.requestSeq().orElseThrow(),
                        inbound.parts());
                }
            });

            TestSupport.awaitCondition(dispatched::get);
            TestSupport.awaitCondition(() -> targetBridge.drain() > 0);
            List<Message> reply = replyStage.toCompletableFuture()
                .get(3, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("dispatch-bridge-pong", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    public void spotRouteBridgeRouterDrainCompletesRouterBridgeRequestAcrossContexts()
        throws Exception {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();
        try (Context targetCtx = Zlink.createContext();
             SpotNode targetNode = targetCtx.createSpotNode();
             Spot targetSpot = targetNode.createSpot();
             RouterSocket targetRouter = targetCtx.createRouterSocket();
             SpotRouteBridge targetBridge = targetNode.createRouteBridge();
             Context sourceCtx = Zlink.createContext();
             SpotNode sourceNode = sourceCtx.createSpotNode();
             RouterSocket sourceRouter = sourceCtx.createRouterSocket();
             SpotRouteBridge sourceBridge = sourceNode.createRouteBridge()) {
            targetSpot.setRoutingId(RoutingId.from("target-spot-cross-context"));
            RoutingId targetRouterRid = RoutingId.from("target-router-cross-context");
            targetRouter.setRoutingId(targetRouterRid);
            sourceRouter.setRoutingId(RoutingId.from("source-router-cross-context"));
            targetRouter.bind(endpoint);
            targetBridge.attachRouterChannel(
                "ingress",
                targetRouter,
                new SpotRouteBridgeEndpointOptions());

            sourceRouter.connect(endpoint);
            Thread.sleep(50);
            sourceBridge.attachRouterChannel("egress", sourceRouter);

            CompletionStage<List<Message>> replyStage =
                sourceBridge.request("egress", targetRouterRid, targetSpot.getRoutingId())
                    .message(Message.from("cross-context-ping"))
                    .timeout(Duration.ofSeconds(2))
                    .submit();

            TestSupport.awaitCondition(() -> {
                try (Received inbound = new Received()) {
                    if (!targetRouter.recv(inbound, RecvFlags.DONT_WAIT)) {
                        return false;
                    }
                    return targetBridge.handleRouterReceived(
                        "ingress",
                        inbound.getRoutingId().orElseThrow(),
                        inbound.requestSeq().orElseThrow(),
                        inbound.parts());
                }
            });

            TestSupport.awaitCondition(() -> {
                try (Received routed = new Received()) {
                    if (!targetSpot.recvRouted(routed, RecvFlags.DONT_WAIT)) {
                        return false;
                    }
                    assertEquals("cross-context-ping",
                        routed.parts().get(0).toUtf8String());
                    routed.reply()
                        .message(Message.from("cross-context-pong"))
                        .submit();
                    return true;
                }
            });

            TestSupport.awaitCondition(() -> targetBridge.drain() > 0);
            List<Message> reply = replyStage.toCompletableFuture()
                .get(3, TimeUnit.SECONDS);
            try {
                assertEquals(1, reply.size());
                assertEquals("cross-context-pong", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    public void spotRouteBridgeRouterDrainCompletesRequestsFromSequentialRouterContexts()
        throws Exception {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();
        try (Context targetCtx = Zlink.createContext();
             SpotNode targetNode = targetCtx.createSpotNode();
             Spot targetSpot = targetNode.createSpot();
             RouterSocket targetRouter = targetCtx.createRouterSocket();
             SpotRouteBridge targetBridge = targetNode.createRouteBridge()) {
            targetSpot.setRoutingId(RoutingId.from("target-spot-sequential-contexts"));
            RoutingId targetRouterRid = RoutingId.from("target-router-sequential-contexts");
            targetRouter.setRoutingId(targetRouterRid);
            targetRouter.bind(endpoint);
            targetBridge.attachRouterChannel(
                "ingress",
                targetRouter,
                new SpotRouteBridgeEndpointOptions());

            for (int i = 0; i < 2; i++) {
                String requestText = "sequential-ping-" + i;
                String replyText = "sequential-pong-" + i;
                try (Context sourceCtx = Zlink.createContext();
                     SpotNode sourceNode = sourceCtx.createSpotNode();
                     RouterSocket sourceRouter = sourceCtx.createRouterSocket();
                     SpotRouteBridge sourceBridge = sourceNode.createRouteBridge()) {
                    sourceRouter.setRoutingId(RoutingId.from("source-router-sequential-" + i));
                    sourceRouter.connect(endpoint);
                    Thread.sleep(50);
                    sourceBridge.attachRouterChannel("egress", sourceRouter);

                    CompletionStage<List<Message>> replyStage =
                        sourceBridge.request("egress", targetRouterRid, targetSpot.getRoutingId())
                            .message(Message.from(requestText))
                            .timeout(Duration.ofSeconds(2))
                            .submit();

                    TestSupport.awaitCondition(() -> {
                        try (Received inbound = new Received()) {
                            if (!targetRouter.recv(inbound, RecvFlags.DONT_WAIT)) {
                                return false;
                            }
                            return targetBridge.handleRouterReceived(
                                "ingress",
                                inbound.getRoutingId().orElseThrow(),
                                inbound.requestSeq().orElseThrow(),
                                inbound.parts());
                        }
                    });

                    TestSupport.awaitCondition(() -> {
                        try (Received routed = new Received()) {
                            if (!targetSpot.recvRouted(routed, RecvFlags.DONT_WAIT)) {
                                return false;
                            }
                            assertEquals(requestText,
                                routed.parts().get(0).toUtf8String());
                            routed.reply()
                                .message(Message.from(replyText))
                                .submit();
                            return true;
                        }
                    });

                    TestSupport.awaitCondition(() -> targetBridge.drain() > 0);
                    List<Message> reply = replyStage.toCompletableFuture()
                        .get(3, TimeUnit.SECONDS);
                    try {
                        assertEquals(1, reply.size());
                        assertEquals(replyText, reply.get(0).toUtf8String());
                    } finally {
                        reply.forEach(Message::close);
                    }
                }
            }
        }
    }

    @Test
    public void spotNodePublisherPublishesIntoLocalTopicPlane() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot subscriber = node.entrySpot();
             SpotNodePublisher publisher = node.createPublisher()) {
            subscriber.setSubscription("bridge-topic");
            assertTrue(publisher.publish("bridge-topic")
                .message(Message.from("topic-payload"))
                .submit());
            try (TopicMessage received = new TopicMessage()) {
                TestSupport.awaitCondition(() ->
                    subscriber.subscribe(received, RecvFlags.DONT_WAIT));
                assertEquals("bridge-topic", received.topic());
                assertEquals("topic-payload",
                    received.parts().get(0).toUtf8String());
            }
        }
    }

    @Test
    public void sendNoWaitReturnsBackpressuredWhenPairQueueFills() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket sender = ctx.createPairSocket();
             PairSocket receiver = ctx.createPairSocket()) {
            sender.options().sendHwm(1);
            receiver.options().recvHwm(1);
            String endpoint = TestSupport.inprocEndpoint("pair-backpressure");
            sender.bind(endpoint);
            receiver.connect(endpoint);
            try {
                Thread.sleep(50L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(e);
            }

            boolean sent = true;
            for (int i = 0; i < 1_024; i++) {
                try (Message payload = Message.from("bp-" + i)) {
                    sent = sender.send()
                        .message(payload)
                        .flags(SendFlags.DONT_WAIT)
                        .submit();
                }
                if (!sent) {
                    break;
                }
            }
            assertFalse(sent);
        }
    }

    @Test
    public void sendDontWaitReturnsFalseWhenPairQueueFills() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket sender = ctx.createPairSocket();
             PairSocket receiver = ctx.createPairSocket()) {
            sender.options().sendHwm(1);
            receiver.options().recvHwm(1);
            String endpoint = TestSupport.inprocEndpoint("pair-try-backpressure");
            sender.bind(endpoint);
            receiver.connect(endpoint);
            try {
                Thread.sleep(50L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(e);
            }

            boolean sent = true;
            for (int i = 0; i < 1_024; i++) {
                try (Message payload = Message.from("bp-" + i)) {
                    sent = sender.send().message(payload).flags(SendFlags.DONT_WAIT).submit();
                }
                if (!sent) {
                    break;
                }
            }
            assertFalse(sent);
        }
    }
}
