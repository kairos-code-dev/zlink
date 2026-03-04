package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestDealerRouterPortedTest {
    @Test
    public void testDealerRouterMessaging() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                try (Socket router = new Socket(ctx, SocketType.ROUTER);
                     Socket dealer = new Socket(ctx, SocketType.DEALER)) {
                    String endpoint = IntegrationSupport.endpointFor(transport,
                        "dealer-router");
                    router.bind(endpoint);
                    dealer.connect(endpoint);
                    TestSupport.sleepMs(60);

                    dealer.send("hello".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);

                    byte[] rid = TestSupport.recvWithTimeout(router, 256,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    byte[] payload = TestSupport.recvWithTimeout(router, 256,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals("hello", new String(payload,
                        StandardCharsets.UTF_8));

                    router.send(rid, SendFlag.SNDMORE);
                    router.send("world".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);
                    byte[] response = TestSupport.recvWithTimeout(dealer, 64,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals("world", new String(response,
                        StandardCharsets.UTF_8));
                }
            }
        }
    }
}
