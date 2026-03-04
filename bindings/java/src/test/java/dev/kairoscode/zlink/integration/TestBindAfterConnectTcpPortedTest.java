package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestBindAfterConnectTcpPortedTest {
    @Test
    public void testConnectBeforeBindQueuesMessages() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket binder = new Socket(ctx, SocketType.DEALER);
             Socket connector = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.tcpEndpoint();

            connector.connect(endpoint);
            connector.send("foobar".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            connector.send("baz".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            connector.send("buzz".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            binder.bind(endpoint);

            assertEquals("foobar", new String(TestSupport.recvWithTimeout(
                binder, 32, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
            assertEquals("baz", new String(TestSupport.recvWithTimeout(
                binder, 32, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
            assertEquals("buzz", new String(TestSupport.recvWithTimeout(
                binder, 32, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
        }
    }
}
