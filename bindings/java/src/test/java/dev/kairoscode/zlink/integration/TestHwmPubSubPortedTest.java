package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestHwmPubSubPortedTest {
    @Test
    public void testDefaultSocketHwmIs1000() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.PAIR)) {
            assertEquals(1000, socket.getSockOptInt(SocketOption.SNDHWM));
            assertEquals(1000, socket.getSockOptInt(SocketOption.RCVHWM));
        }
    }

    @Test
    public void testXpubSubRespectsConfiguredHwm() {
        TestSupport.assumeNative();

        final int hwm = 100;
        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            xpub.setSockOpt(SocketOption.SNDHWM, hwm);
            sub.setSockOpt(SocketOption.RCVHWM, hwm);
            xpub.setSockOpt(SocketOption.XPUB_NODROP, 1);
            sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);

            String endpoint = TestSupport.tcpEndpoint();
            xpub.bind(endpoint);
            sub.connect(endpoint);

            // Wait until subscription reaches XPUB.
            TestSupport.recvWithTimeout(xpub, 16, TestSupport.DEFAULT_TIMEOUT_MS);

            int sendCount = 0;
            byte[] payload = "x".getBytes(StandardCharsets.UTF_8);
            for (int i = 0; i < 5000; i++) {
                try {
                    xpub.send(payload, SendFlag.DONTWAIT);
                    sendCount++;
                } catch (RuntimeException ex) {
                    break;
                }
            }

            assertTrue(sendCount > 0);
            assertTrue(sendCount < 5000,
                "expected backpressure, sendCount=" + sendCount);
            assertTrue(sendCount <= hwm + 512,
                "sendCount=" + sendCount + ", hwm=" + hwm);

            TestSupport.sleepMs(100);

            int recvCount = 0;
            for (;;) {
                try {
                    sub.recv(8, ReceiveFlag.DONTWAIT);
                    recvCount++;
                } catch (RuntimeException ex) {
                    break;
                }
            }
            assertEquals(sendCount, recvCount);
        }
    }
}
