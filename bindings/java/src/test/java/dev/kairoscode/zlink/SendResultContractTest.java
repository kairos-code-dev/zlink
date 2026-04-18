package dev.kairoscode.zlink;

import dev.kairoscode.zlink.SocketOptions;
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
            RecvException ex = assertThrows(RecvException.class,
                () -> sub.subscribe(RecvFlags.DONT_WAIT));
            assertEquals(RecvResult.NO_DATA, ex.getResult());
        }
    }

    @Test
    public void receiveSubscriptionEventDontWaitReturnsNoDataWhenNoEventExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             XPubSocket pub = new XPubSocket(ctx)) {
            RecvException ex = assertThrows(RecvException.class,
                () -> pub.receiveSubscriptionEvent(RecvFlags.DONT_WAIT));
            assertEquals(RecvResult.NO_DATA, ex.getResult());
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
    public void trySendThrowsWhenRouterHasNoMatchingPeer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             Message payload = Message.copyOfUtf8("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.fromBytes(
                "router-missing-peer".getBytes(StandardCharsets.UTF_8));
            SubmitException ex = assertThrows(SubmitException.class,
                () -> router.trySend(missingRid, payload));
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
    public void trySendReturnsFalseWhenPairQueueFills() {
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
                    sent = sender.trySend(payload);
                }
                if (!sent) {
                    break;
                }
            }
            assertFalse(sent);
        }
    }
}
