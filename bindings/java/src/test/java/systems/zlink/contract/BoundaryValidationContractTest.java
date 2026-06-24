package systems.zlink.contract;

import systems.zlink.TestSupport;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.RouterSocketOptions;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.sockets.SubSocket;
import java.lang.reflect.Method;
import java.time.Duration;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class BoundaryValidationContractTest {
    @Test
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             StreamSocket stream = ctx.createStreamSocket();
             SpotNode node = ctx.createSpotNode(SpotNodeMode.PUBSUB)) {
            ZlinkConfigException error = assertThrows(ZlinkConfigException.class,
            assertEquals(ConfigResult.NOT_SUPPORTED, error.getResult());
        }
    }

    @Test
    public void routingIdAcceptsMaximumLengthAndRejectsOverflow() {
        byte[] max = new byte[RoutingId.MAX_LENGTH];
        byte[] overflow = new byte[RoutingId.MAX_LENGTH + 1];

        RoutingId routingId = assertDoesNotThrow(() -> RoutingId.from(max));
        assertEquals(RoutingId.MAX_LENGTH, routingId.size());
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.from(overflow));
    }

    @Test
    public void routingIdParsesHexString() {
        RoutingId routingId = RoutingId.from(new byte[] {0x00, 0x41, 0x42});

        assertEquals(routingId, RoutingId.fromHex("004142"));
        assertEquals("004142", routingId.toHex());
        assertEquals("hex:004142", routingId.toString());
        assertEquals(RoutingId.MAX_LENGTH, RoutingId.fromHex("a".repeat(510)).size());
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.fromHex("not-hex"));
        assertThrows(IllegalArgumentException.class,
            () -> RoutingId.fromHex("a".repeat(512)));
        assertEquals("dealer-1", RoutingId.from("dealer-1").toString());
        assertEquals("23", RoutingId.from(23L).toString());
    }

    @Test
    public void routingIdDoesNotExposeUint32Factory() {
        assertFalse(hasPublicMethod(RoutingId.class, "fromU32", int.class));
    }

    @Test
    public void durationTimeoutRejectsIntOverflow() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket router = ctx.createRouterSocket()) {
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

        try (Context ctx = Zlink.createContext();
             PubSocket pub = ctx.createPubSocket();
             SubSocket sub = ctx.createSubSocket();
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
