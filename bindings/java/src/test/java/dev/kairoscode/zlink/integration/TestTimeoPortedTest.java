package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

public class TestTimeoPortedTest {
    @Test
    public void testReceiveAndSendTimeout() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket frontend = new Socket(ctx, SocketType.DEALER);
             Socket backend = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.inprocEndpoint("timeo");
            frontend.bind(endpoint);

            int timeoutMs = 250;
            frontend.setSockOpt(SocketOption.RCVTIMEO, timeoutMs);

            long start = System.nanoTime();
            try {
                frontend.recv(32, dev.kairoscode.zlink.ReceiveFlag.NONE);
                fail("expected timeout");
            } catch (RuntimeException expected) {
                long elapsedMs = (System.nanoTime() - start) / 1_000_000L;
                assertTrue(elapsedMs >= timeoutMs - 50,
                    "elapsedMs=" + elapsedMs);
            }

            backend.connect(endpoint);
            backend.setSockOpt(SocketOption.SNDTIMEO, timeoutMs);
            backend.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            byte[] out = frontend.recv(32,
                dev.kairoscode.zlink.ReceiveFlag.NONE);
            assertTrue(new String(out, StandardCharsets.UTF_8).equals("Hello"));
        }
    }
}
