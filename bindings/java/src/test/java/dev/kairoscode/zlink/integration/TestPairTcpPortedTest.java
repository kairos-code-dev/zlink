package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestPairTcpPortedTest {
    @Test
    public void testPairTcpRoundTrip() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);
            client.connect(endpoint);
            TestSupport.sleepMs(40);

            client.send("ping".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("ping", new String(TestSupport.recvWithTimeout(server,
                16, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));

            server.send("pong".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("pong", new String(TestSupport.recvWithTimeout(client,
                16, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }
}
