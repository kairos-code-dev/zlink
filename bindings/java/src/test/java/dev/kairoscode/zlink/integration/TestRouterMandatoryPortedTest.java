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
import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestRouterMandatoryPortedTest {
    @Test
    public void testRouterMandatoryRouting() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);

            // Default behavior: unknown routing id is accepted then dropped.
            router.send("UNKNOWN".getBytes(StandardCharsets.UTF_8),
                SendFlag.SNDMORE);
            router.send("DATA".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            router.setSockOpt(SocketOption.ROUTER_MANDATORY, 1);
            assertThrows(RuntimeException.class,
                () -> router.send("UNKNOWN".getBytes(StandardCharsets.UTF_8),
                    SendFlag.SNDMORE));

            dealer.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            dealer.connect(endpoint);

            dealer.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("X", new String(TestSupport.recvWithTimeout(router, 64,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(router,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));

            router.send("X".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            router.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(dealer,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }
}
