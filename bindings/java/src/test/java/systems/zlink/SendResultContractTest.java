package systems.zlink;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
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
    public void spotRouteBridgeRequestUsesCallerOwnedDealerSocket() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot();
             DealerSocket dealer = ctx.createDealerSocket();
             RouterSocket router = ctx.createRouterSocket();
             SpotRouteBridge bridge = node.createRouteBridge()) {
            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);
            dealer.connect(endpoint);
            bridge.attachDealerChannel("api", dealer);

            CompletionStage<List<Message>> replyFuture =
                bridge.request("api", spot.getRoutingId())
                    .message(Message.from("spot-ping"))
                    .timeout(Duration.ofSeconds(2))
                    .submit();

            try (Received received = new Received()) {
                TestSupport.awaitCondition(() ->
                    router.recv(received, RecvFlags.DONT_WAIT));
                assertEquals("spot-ping",
                    received.parts().get(received.parts().size() - 1)
                        .toUtf8String());
                received.reply().message(Message.from("spot-pong")).submit();
            }

            List<Message> reply = replyFuture.toCompletableFuture().join();
            try {
                assertEquals(1, reply.size());
                assertEquals("spot-pong", reply.get(0).toUtf8String());
            } finally {
                reply.forEach(Message::close);
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
