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
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestProbeRouterPortedTest {
    @Test
    public void testProbeRouterWithRouterClient() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.ROUTER);
             Socket client = new Socket(ctx, SocketType.ROUTER)) {
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);

            client.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            client.setSockOpt(SocketOption.PROBE_ROUTER, 1);
            client.connect(endpoint);

            assertEquals("X", new String(TestSupport.recvWithTimeout(server,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            assertEquals(0, TestSupport.recvWithTimeout(server, 64,
                TestSupport.DEFAULT_TIMEOUT_MS).length);

            server.send("X".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            server.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            byte[] peerRid = TestSupport.recvWithTimeout(client, 255,
                TestSupport.DEFAULT_TIMEOUT_MS);
            assertTrue(peerRid.length > 0);
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(client,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }

    @Test
    public void testProbeRouterWithDealerClient() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.ROUTER);
             Socket client = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);

            client.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            client.setSockOpt(SocketOption.PROBE_ROUTER, 1);
            client.connect(endpoint);

            assertEquals("X", new String(TestSupport.recvWithTimeout(server,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.recvWithTimeout(server, 64, TestSupport.DEFAULT_TIMEOUT_MS);

            server.send("X".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            server.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(client,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }
}
