package io.ulalax.zlink.integration;

import io.ulalax.zlink.Context;
import io.ulalax.zlink.SendFlag;
import io.ulalax.zlink.Socket;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class MultipartScenarioTest {
    @Test
    public void multipartPair() {
        try (Context ctx = new Context()) {
            for (TestTransports.TransportCase tc : TestTransports.transports("multipart")) {
                TestTransports.tryTransport(tc.name, () -> {
                    try (Socket a = new Socket(ctx, TestTransports.ZLINK_PAIR);
                         Socket b = new Socket(ctx, TestTransports.ZLINK_PAIR)) {
                        String ep = TestTransports.endpointFor(tc.name, tc.endpoint, "-mp");
                        a.bind(ep);
                        b.connect(ep);
                        sleep(50);
                        TestTransports.sendWithRetry(b, "a".getBytes(),
                            TestTransports.ZLINK_SNDMORE, 2000);
                        TestTransports.sendWithRetry(b, "b".getBytes(), SendFlag.NONE, 2000);
                        byte[] p1 = TestTransports.recvWithTimeout(a, 16, 2000);
                        byte[] p2 = TestTransports.recvWithTimeout(a, 16, 2000);
                        assertEquals("a", new String(p1).trim());
                        assertEquals("b", new String(p2).trim());
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
