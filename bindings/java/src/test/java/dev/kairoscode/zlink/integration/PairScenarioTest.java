package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class PairScenarioTest {
    @Test
    public void pairMessaging() {
        try (Context ctx = new Context()) {
            for (TestTransports.TransportCase tc : TestTransports.transports("pair")) {
                TestTransports.tryTransport(tc.name, () -> {
                    try (Socket a = new Socket(ctx, TestTransports.ZLINK_PAIR);
                         Socket b = new Socket(ctx, TestTransports.ZLINK_PAIR)) {
                        String ep = TestTransports.endpointFor(tc.name, tc.endpoint, "-pair");
                        a.bind(ep);
                        b.connect(ep);
                        sleep(50);
                        TestTransports.sendWithRetry(b, "ping".getBytes(), SendFlag.NONE, 2000);
                        byte[] out = TestTransports.recvWithTimeout(a, 16, 2000);
                        assertEquals("ping", new String(out, 0, 4));
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
