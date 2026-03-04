package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestXpubXsubPortedTest {
    @Test
    public void testXpubXsubSubscriptionForwarding() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                try (Socket xpub = new Socket(ctx, SocketType.XPUB);
                     Socket xsub = new Socket(ctx, SocketType.XSUB)) {
                    xpub.setSockOpt(SocketOption.XPUB_VERBOSE, 1);

                    String endpoint = IntegrationSupport.endpointFor(transport,
                        "xpub-xsub");
                    xpub.bind(endpoint);
                    xsub.connect(endpoint);
                    TestSupport.sleepMs(60);

                    byte[] sub = new byte[]{1, 't', 'o', 'p', 'i', 'c'};
                    xsub.send(sub, SendFlag.NONE);

                    byte[] msg = TestSupport.recvWithTimeout(xpub, 64,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals(1, msg[0]);
                }
            }
        }
    }
}
