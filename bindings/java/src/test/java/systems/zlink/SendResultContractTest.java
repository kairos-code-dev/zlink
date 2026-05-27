package systems.zlink;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SendResult;
import systems.zlink.contracts.sockets.SocketOptions;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.SubSocket;
import systems.zlink.contracts.errors.SubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.XPubSocket;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class SendResultContractTest {
    @Test
    public void subscribeDontWaitReturnsNoDataWhenNoTopicDeliveryExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SubSocket sub = new SubSocket(ctx)) {
            assertFalse(sub.subscribe(new TopicMessage(), RecvFlags.DONT_WAIT));
        }
    }

    @Test
    public void spotSubscribeDontWaitReturnsNoDataWhenNoTopicDeliveryExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot()) {
            assertFalse(spot.subscribe(new TopicMessage(), RecvFlags.DONT_WAIT));
        }
    }

    @Test
    public void receiveSubscriptionEventDontWaitReturnsNoDataWhenNoEventExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             XPubSocket pub = new XPubSocket(ctx)) {
            assertFalse(pub.receiveSubscriptionEvent(
                new SubscriptionEvent(), RecvFlags.DONT_WAIT));
        }
    }

    @Test
    public void sendNoWaitReturnsNotReadyWhenRouterHasNoMatchingPeer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             Message payload = Message.from("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.from(
                "router-missing-peer".getBytes(StandardCharsets.UTF_8));
            SendResult result = router.sendNoWaitResult(missingRid, payload);
            assertEquals(SendResult.NOT_READY, result);
        }
    }

    @Test
    public void sendDontWaitThrowsWhenRouterHasNoMatchingPeer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             Message payload = Message.from("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.from(
                "router-missing-peer".getBytes(StandardCharsets.UTF_8));
            SubmitException ex = assertThrows(SubmitException.class,
                () -> router.send(missingRid)
                    .message(payload)
                    .flags(SendFlags.DONT_WAIT)
                    .submit());
            assertEquals(SubmitResult.NOT_CONNECTED, ex.getResult());
        }
    }

    @Test
    public void sendNoWaitReturnsBackpressuredWhenPairQueueFills() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket sender = new PairSocket(ctx);
             PairSocket receiver = new PairSocket(ctx)) {
            sender.setOption(SocketOptions.SNDHWM, 1);
            receiver.setOption(SocketOptions.RCVHWM, 1);
            String endpoint = TestSupport.inprocEndpoint("pair-backpressure");
            sender.bind(endpoint);
            receiver.connect(endpoint);
            try {
                Thread.sleep(50L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(e);
            }

            SendResult result = SendResult.SENT;
            for (int i = 0; i < 1_024; i++) {
                try (Message payload = Message.from("bp-" + i)) {
                    result = sender.sendNoWaitResult(payload);
                }
                if (result != SendResult.SENT) {
                    break;
                }
            }
            assertEquals(SendResult.BACKPRESSURED, result);
        }
    }

    @Test
    public void sendDontWaitReturnsFalseWhenPairQueueFills() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket sender = new PairSocket(ctx);
             PairSocket receiver = new PairSocket(ctx)) {
            sender.setOption(SocketOptions.SNDHWM, 1);
            receiver.setOption(SocketOptions.RCVHWM, 1);
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
