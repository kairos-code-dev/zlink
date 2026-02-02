package io.ulalax.zlink.integration;

import io.ulalax.zlink.Context;
import io.ulalax.zlink.Socket;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

public class PubSubScenarioTest {
    @Test
    public void pubSubMessaging() {
        try (Context ctx = new Context()) {
            for (TestTransports.TransportCase tc : TestTransports.transports("pubsub")) {
                TestTransports.tryTransport(tc.name, () -> {
                    try (Socket pub = new Socket(ctx, TestTransports.ZLINK_PUB);
                         Socket sub = new Socket(ctx, TestTransports.ZLINK_SUB)) {
                        String ep = TestTransports.endpointFor(tc.name, tc.endpoint, "-pubsub");
                        pub.bind(ep);
                        sub.connect(ep);
                        sub.setSockOpt(TestTransports.ZLINK_SUBSCRIBE, "topic".getBytes());
                        sleep(50);
                        TestTransports.sendWithRetry(pub, "topic payload".getBytes(), 0, 2000);
                        byte[] buf = TestTransports.recvWithTimeout(sub, 64, 2000);
                        String out = new String(buf);
                        assertTrue(out.startsWith("topic"));
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
