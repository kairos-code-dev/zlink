package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;

final class ZLinkJavaRawServicePortContractTest {
    @Test
    void closesOwnedRawResourcesOnceAndRejectsNewResourcesAfterClose() {
        ZLinkJavaRawServicePort port = new ZLinkJavaRawServicePort();
        port.openRouter(RoutingId.from("m5-resource-owner"));

        port.close();

        assertDoesNotThrow(port::close);
        assertThrows(IllegalStateException.class,
            () -> port.openRouter(RoutingId.from("after-close")));
    }

    @Test
    void sendsAndReceivesCopiedMultipartWithSourceRoutingId() throws Exception {
        RoutingId leftRid = RoutingId.from("m5-left");
        RoutingId rightRid = RoutingId.from("m5-right");
        String endpoint = "inproc://m5-raw-service-port-" + System.nanoTime();
        byte[] first = new byte[] {1, 2, 3};
        byte[] second = new byte[] {4, 5, 6};

        try (ZLinkJavaRawServicePort port = new ZLinkJavaRawServicePort()) {
            var left = port.openRouter(leftRid);
            var right = port.openRouter(rightRid);
            left.bind(endpoint);
            right.connect(endpoint);

            long deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (!port.send(right, leftRid, List.of(first, second))) {
                if (System.nanoTime() >= deadline) {
                    throw new AssertionError("raw multipart send did not become ready");
                }
                Thread.sleep(1);
            }
            first[0] = 9;
            second[0] = 9;

            Optional<ZLinkJavaRawServicePort.Inbound> received = Optional.empty();
            while (received.isEmpty()) {
                received = port.receive(left);
                if (System.nanoTime() >= deadline) {
                    throw new AssertionError("raw multipart receive timed out");
                }
                if (received.isEmpty()) {
                    Thread.sleep(1);
                }
            }

            assertEquals(rightRid, received.orElseThrow().source());
            List<byte[]> retained = received.orElseThrow().frames();
            assertArrayEquals(new byte[] {1, 2, 3}, retained.get(0));
            assertArrayEquals(new byte[] {4, 5, 6}, retained.get(1));
            retained.get(0)[0] = 8;
            assertArrayEquals(
                new byte[] {1, 2, 3},
                received.orElseThrow().frames().get(0));
        }
    }
}
