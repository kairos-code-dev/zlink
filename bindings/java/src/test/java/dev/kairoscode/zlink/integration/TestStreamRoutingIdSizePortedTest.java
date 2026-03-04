package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestStreamRoutingIdSizePortedTest {
    @Test
    public void testStreamAutoRoutingIdSizeIsFour() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.STREAM)) {
            String endpoint = TestSupport.tcpEndpoint();
            int port = parsePort(endpoint);
            server.bind(endpoint);

            CountDownLatch called = new CountDownLatch(1);
            AtomicInteger ridLength = new AtomicInteger(-1);
            AtomicReference<byte[]> payloadRef = new AtomicReference<>(new byte[0]);

            server.attachStreamRaw((routingId, payload) -> {
                try (Message msg = payload) {
                    ridLength.set(4);
                    payloadRef.set(msg.data());
                    called.countDown();
                }
                return 0;
            });

            try (java.net.Socket raw = new java.net.Socket("127.0.0.1", port)) {
                OutputStream out = raw.getOutputStream();
                out.write('x');
                out.flush();
            }

            assertTrue(called.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertEquals(4, ridLength.get());
            assertEquals("x", new String(payloadRef.get(),
                StandardCharsets.UTF_8));

            server.detachStream();
        }
    }

    private static int parsePort(String endpoint) {
        int idx = endpoint.lastIndexOf(':');
        if (idx <= 0 || idx == endpoint.length() - 1)
            throw new IllegalArgumentException("invalid endpoint: " + endpoint);
        return Integer.parseInt(endpoint.substring(idx + 1));
    }
}
