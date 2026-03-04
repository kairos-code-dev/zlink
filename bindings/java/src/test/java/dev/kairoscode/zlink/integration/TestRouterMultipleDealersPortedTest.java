package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestRouterMultipleDealersPortedTest {
    @Test
    public void testRouterMultipleDealersTcp() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer1 = new Socket(ctx, SocketType.DEALER);
             Socket dealer2 = new Socket(ctx, SocketType.DEALER)) {
            dealer1.setSockOpt(SocketOption.ROUTING_ID,
                "D1".getBytes(StandardCharsets.UTF_8));
            dealer2.setSockOpt(SocketOption.ROUTING_ID,
                "D2".getBytes(StandardCharsets.UTF_8));

            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);
            dealer1.connect(endpoint);
            dealer2.connect(endpoint);
            TestSupport.sleepMs(60);

            dealer1.send("from_dealer1".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            dealer2.send("from_dealer2".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);

            Map<String, String> inbound = new HashMap<>();
            for (int i = 0; i < 2; i++) {
                String rid = new String(TestSupport.recvWithTimeout(router, 32,
                    TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8);
                String payload = new String(TestSupport.recvWithTimeout(router,
                    64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8);
                inbound.put(rid, payload);
            }

            assertEquals("from_dealer1", inbound.get("D1"));
            assertEquals("from_dealer2", inbound.get("D2"));

            router.send("D1".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            router.send("reply_to_d1".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            router.send("D2".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            router.send("reply_to_d2".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);

            assertEquals("reply_to_d1", new String(TestSupport.recvWithTimeout(
                dealer1, 64, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
            assertEquals("reply_to_d2", new String(TestSupport.recvWithTimeout(
                dealer2, 64, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
        }
    }
}
