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
import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestImmediatePortedTest {
    @Test
    public void testImmediateBlocksWhenDisconnectedAndRecoversOnReconnect() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket frontend = new Socket(ctx, SocketType.DEALER)) {
            frontend.setSockOpt(SocketOption.LINGER, 0);
            frontend.setSockOpt(SocketOption.IMMEDIATE, 1);

            String endpoint = TestSupport.tcpEndpoint();

            Socket backend = new Socket(ctx, SocketType.DEALER);
            backend.setSockOpt(SocketOption.LINGER, 0);
            backend.bind(endpoint);
            frontend.connect(endpoint);

            backend.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(frontend,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));

            frontend.send("Hello".getBytes(StandardCharsets.UTF_8),
                SendFlag.DONTWAIT);

            backend.close();
            TestSupport.sleepMs(200);

            assertThrows(RuntimeException.class,
                () -> frontend.send("Hello".getBytes(StandardCharsets.UTF_8),
                    SendFlag.DONTWAIT));

            backend = new Socket(ctx, SocketType.DEALER);
            backend.setSockOpt(SocketOption.LINGER, 0);
            backend.bind(endpoint);

            backend.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(frontend,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));

            frontend.send("Hello".getBytes(StandardCharsets.UTF_8),
                SendFlag.DONTWAIT);
            backend.close();
        }
    }
}
