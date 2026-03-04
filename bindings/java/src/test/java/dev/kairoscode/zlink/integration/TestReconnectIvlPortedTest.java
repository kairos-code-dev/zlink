package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestReconnectIvlPortedTest {
    @Test
    public void testReconnectIvlMinusOneRequiresManualReconnect() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);

            client.setSockOpt(SocketOption.RECONNECT_IVL, -1);
            client.connect(endpoint);
            TestSupport.sleepMs(60);
            bounce(server, client);

            server.unbind(endpoint);
            TestSupport.sleepMs(100);
            assertNoDelivery(server, client);

            server.bind(endpoint);
            TestSupport.sleepMs(100);
            assertNoDelivery(server, client);

            client.connect(endpoint);
            TestSupport.sleepMs(60);
            bounce(server, client);
        }
    }

    private static void bounce(Socket a, Socket b) {
        a.send("a2b".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
        assertEquals("a2b", new String(TestSupport.recvWithTimeout(b, 32,
            TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        b.send("b2a".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
        assertEquals("b2a", new String(TestSupport.recvWithTimeout(a, 32,
            TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
    }

    private static void assertNoDelivery(Socket server, Socket client) {
        try {
            client.send("probe".getBytes(StandardCharsets.UTF_8),
                SendFlag.DONTWAIT);
        } catch (RuntimeException ignored) {
        }
        TestSupport.assertNoMessage(server, 32, 200);
    }
}
