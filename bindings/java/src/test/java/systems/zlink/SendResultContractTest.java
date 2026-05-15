package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.SocketOptions;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
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
             Message payload = Message.copyOfUtf8("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.fromBytes(
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
             Message payload = Message.copyOfUtf8("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.fromBytes(
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
                try (Message payload = Message.copyOfUtf8("bp-" + i)) {
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
                try (Message payload = Message.copyOfUtf8("bp-" + i)) {
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
