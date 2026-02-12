package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class DealerRouterScenarioTest {
    @Test
    public void dealerRouterMessaging() {
        try (Context ctx = new Context()) {
            for (TestTransports.TransportCase tc : TestTransports.transports("dealer-router")) {
                TestTransports.tryTransport(tc.name, () -> {
                    try (Socket router = new Socket(ctx, TestTransports.ZLINK_ROUTER);
                         Socket dealer = new Socket(ctx, TestTransports.ZLINK_DEALER)) {
                        String ep = TestTransports.endpointFor(tc.name, tc.endpoint, "-dr");
                        router.bind(ep);
                        dealer.connect(ep);
                        sleep(50);
                        TestTransports.sendWithRetry(dealer, "hello".getBytes(), SendFlag.NONE, 2000);
                        byte[] rid = TestTransports.recvWithTimeout(router, 256, 2000);
                        byte[] payload = TestTransports.recvWithTimeout(router, 256, 2000);
                        assertEquals("hello", new String(payload).trim());
                        router.send(rid, TestTransports.ZLINK_SNDMORE);
                        TestTransports.sendWithRetry(router, "world".getBytes(), SendFlag.NONE, 2000);
                        byte[] resp = TestTransports.recvWithTimeout(dealer, 64, 2000);
                        assertEquals("world", new String(resp).trim());
                    }
                });
            }
        }
    }

    private static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }
}
