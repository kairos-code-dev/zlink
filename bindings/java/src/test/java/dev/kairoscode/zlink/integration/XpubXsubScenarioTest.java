package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class XpubXsubScenarioTest {
    @Test
    public void xpubXsubSubscription() {
        try (Context ctx = new Context()) {
            for (TestTransports.TransportCase tc : TestTransports.transports("xpub")) {
                TestTransports.tryTransport(tc.name, () -> {
                    try (Socket xpub = new Socket(ctx, TestTransports.ZLINK_XPUB);
                         Socket xsub = new Socket(ctx, TestTransports.ZLINK_XSUB)) {
                        xpub.setSockOpt(TestTransports.ZLINK_XPUB_VERBOSE, 1);
                        String ep = TestTransports.endpointFor(tc.name, tc.endpoint, "-xpub");
                        xpub.bind(ep);
                        xsub.connect(ep);
                        byte[] sub = new byte[]{1, 't', 'o', 'p', 'i', 'c'};
                        TestTransports.sendWithRetry(xsub, sub, SendFlag.NONE, 2000);
                        byte[] msg = TestTransports.recvWithTimeout(xpub, 64, 2000);
                        assertEquals(1, msg[0]);
                    }
                });
            }
        }
    }
}
