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

public class TestPubSubPortedTest {
    @Test
    public void testPubSubAcrossCoreTransports() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                try (Socket pub = new Socket(ctx, SocketType.PUB);
                     Socket sub = new Socket(ctx, SocketType.SUB)) {
                    String endpoint = IntegrationSupport.endpointFor(transport,
                        "pubsub");
                    pub.bind(endpoint);
                    sub.connect(endpoint);
                    sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);
                    TestSupport.sleepMs(80);

                    pub.send("test".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);
                    byte[] out = TestSupport.recvWithTimeout(sub, 64,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals("test", new String(out,
                        StandardCharsets.UTF_8));
                }
            }
        }
    }
}
