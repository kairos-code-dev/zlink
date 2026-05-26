package systems.zlink.contract;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.ConfigException;
import systems.zlink.contracts.ConfigResult;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.PubSocket;
import systems.zlink.contracts.RouterSocket;
import systems.zlink.contracts.RouterSocketOptions;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.StreamSocket;
import systems.zlink.contracts.SubSocket;
import systems.zlink.contracts.TestSupport;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import java.lang.reflect.Method;
import java.time.Duration;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class BoundaryValidationContractTest {
    @Test
    public void streamAttachActorGatewayRequiresRoutedNode() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             StreamSocket stream = new StreamSocket(ctx);
             SpotNode node = new SpotNode(ctx, SpotNodeMode.PUBSUB)) {
            ConfigException error = assertThrows(ConfigException.class,
                () -> stream.attachActorGateway(node));
            assertEquals(ConfigResult.NOT_SUPPORTED, error.getResult());
        }
    }

    @Test
    public void routingIdAcceptsMaximumLengthAndRejectsOverflow() {
        byte[] max = new byte[RoutingId.MAX_LENGTH];
        byte[] overflow = new byte[RoutingId.MAX_LENGTH + 1];

        RoutingId routingId = assertDoesNotThrow(() -> RoutingId.fromBytes(max));
        assertEquals(RoutingId.MAX_LENGTH, routingId.size());
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.fromBytes(overflow));
    }

    @Test
    public void routingIdParsesHexString() {
        RoutingId routingId = RoutingId.fromBytes(new byte[] {0x00, 0x41, 0x42});

        assertEquals(routingId, RoutingId.fromString("004142"));
        assertEquals("004142", routingId.toHex());
        assertEquals(RoutingId.MAX_LENGTH, RoutingId.fromString("a".repeat(510)).size());
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.fromString("not-hex"));
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.fromString("a".repeat(512)));
    }

    @Test
    public void routingIdDoesNotExposeUint32Factory() {
        assertFalse(hasPublicMethod(RoutingId.class, "fromU32", int.class));
    }

    @Test
    public void durationTimeoutRejectsIntOverflow() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx)) {
            RouterSocketOptions options = router.options();
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
            () -> Message.from(payload, 1, 3));
    }

    @Test
    public void topicAndFilterRejectOverflowingUtf8Length() {
        TestSupport.assumeNative();

        String max = "a".repeat(255);
        String overflow = "b".repeat(256);

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             Message payload = Message.from("payload")) {
            assertDoesNotThrow(() -> sub.setSubscription(max));
            assertThrows(IllegalArgumentException.class,
                () -> sub.setSubscription(overflow));

            assertDoesNotThrow(() -> pub.publish(max).message(payload).submit());
            assertThrows(IllegalArgumentException.class,
                () -> pub.publish(overflow).message(payload).submit());
        }
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameterTypes) {
        try {
            Method method = type.getMethod(name, parameterTypes);
            return method != null;
        } catch (NoSuchMethodException ex) {
            return false;
        }
    }
}
