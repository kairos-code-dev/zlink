package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.CommonSocketOptions;
import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.TestSupport;
import java.time.Duration;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class BoundaryValidationContractTest {
    @Test
    public void routingIdAcceptsMaximumLengthAndRejectsOverflow() {
        byte[] max = new byte[RoutingId.MAX_LENGTH];
        byte[] overflow = new byte[RoutingId.MAX_LENGTH + 1];

        RoutingId routingId = assertDoesNotThrow(() -> RoutingId.copyOf(max));
        assertEquals(RoutingId.MAX_LENGTH, routingId.size());
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.copyOf(overflow));
    }

    @Test
    public void durationTimeoutRejectsIntOverflow() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx)) {
            CommonSocketOptions options = router.options();
            assertThrows(IllegalArgumentException.class,
                () -> options.recvTimeout(Duration.ofMillis((long) Integer.MAX_VALUE + 1)));
            assertThrows(IllegalArgumentException.class,
                () -> options.sendTimeout(Duration.ofMillis((long) Integer.MIN_VALUE - 1)));
        }
    }

    @Test
    public void messageCopyOfRejectsOutOfBoundsRange() {
        byte[] payload = new byte[] {1, 2, 3};
        assertThrows(IndexOutOfBoundsException.class,
            () -> Message.copyOf(payload, 1, 3));
    }

    @Test
    public void topicAndFilterRejectOverflowingUtf8Length() {
        TestSupport.assumeNative();

        String max = "a".repeat(255);
        String overflow = "b".repeat(256);

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             Message payload = Message.copyOfUtf8("payload")) {
            assertDoesNotThrow(() -> sub.setSubscription(max));
            assertThrows(IllegalArgumentException.class,
                () -> sub.setSubscription(overflow));

            assertDoesNotThrow(() -> pub.publish(max, payload));
            assertThrows(IllegalArgumentException.class,
                () -> pub.publish(overflow, payload));
        }
    }
}
