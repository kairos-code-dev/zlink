package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;
import java.nio.charset.StandardCharsets;
import java.util.Optional;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SendResultContractTest {
    @Test
    public void trySubscribeReturnsEmptyWhenNoTopicDeliveryExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SubSocket sub = new SubSocket(ctx)) {
            sub.options().receiveTimeoutMillis(10);
            assertTrue(sub.trySubscribe().isEmpty());
        }
    }

    @Test
    public void tryReceiveSubscriptionEventReturnsEmptyWhenNoEventExists() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             XPubSocket pub = new XPubSocket(ctx)) {
            assertTrue(pub.tryReceiveSubscriptionEvent().isEmpty());
        }
    }

    @Test
    public void trySendReturnsNotReadyWhenRouterHasNoMatchingPeer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             Message payload = Message.copyOfUtf8("router-payload")) {
            router.options().mandatory(true);
            RoutingId missingRid = RoutingId.copyOf(
                "missing-peer".getBytes(StandardCharsets.UTF_8));
            assertEquals(SendResult.NOT_READY,
                router.trySend(missingRid, payload));
        }
    }

    @Test
    public void trySendReturnsBackpressuredWhenPairQueueFills() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx)) {
            server.setOption(SocketOptions.RCVHWM, 1);
            client.setOption(SocketOptions.SNDHWM, 1);
            String endpoint = TestSupport.inprocEndpoint("pair-backpressure");
            server.bind(endpoint);
            client.connect(endpoint);

            SendResult result = SendResult.SENT;
            for (int i = 0; i < 10_000; i++) {
                try (Message payload = Message.copyOfUtf8("bp-" + i)) {
                    result = client.trySend(payload);
                }
                if (result != SendResult.SENT)
                    break;
            }
            assertEquals(SendResult.BACKPRESSURED, result);
        }
    }
}
