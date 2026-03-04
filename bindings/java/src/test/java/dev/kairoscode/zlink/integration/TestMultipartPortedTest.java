package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestMultipartPortedTest {
    @Test
    public void testMultipartPairAcrossCoreTransports() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                try (Socket a = new Socket(ctx, SocketType.PAIR);
                     Socket b = new Socket(ctx, SocketType.PAIR)) {
                    String endpoint = IntegrationSupport.endpointFor(transport,
                        "multipart");
                    a.bind(endpoint);
                    b.connect(endpoint);
                    TestSupport.sleepMs(60);

                    b.send("a".getBytes(StandardCharsets.UTF_8),
                        SendFlag.SNDMORE);
                    b.send("b".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

                    byte[] p1 = TestSupport.recvWithTimeout(a, 16,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    byte[] p2 = TestSupport.recvWithTimeout(a, 16,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals("a", new String(p1, StandardCharsets.UTF_8));
                    assertEquals("b", new String(p2, StandardCharsets.UTF_8));
                }
            }
        }
    }
}
