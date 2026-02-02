package io.ulalax.zlink.integration;

import io.ulalax.zlink.Context;
import io.ulalax.zlink.Socket;
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
                        TestTransports.sendWithRetry(b, "ping".getBytes(), 0, 2000);
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
