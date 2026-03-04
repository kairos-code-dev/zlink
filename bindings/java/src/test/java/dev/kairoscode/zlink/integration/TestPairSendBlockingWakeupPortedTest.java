package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestPairSendBlockingWakeupPortedTest {
    private static final int PIPE_HWM = 100;
    private static final int SOCKET_HWM = PIPE_HWM / 2;
    private static final int LWM = (PIPE_HWM + 1) / 2;
    private static final int SEND_TIMEOUT_MS = 2000;
    private static final int MSG_SIZE = 64;
    private static final byte[] PAYLOAD = filledPayload(MSG_SIZE, (byte) 'a');

    @Test
    public void testPairBlockingSendWakesOnlyAfterLwmReads() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket receiver = new Socket(ctx, SocketType.PAIR);
             Socket sender = new Socket(ctx, SocketType.PAIR)) {
            configurePairSocket(receiver);
            configurePairSocket(sender);
            sender.setSockOpt(SocketOption.SNDTIMEO, SEND_TIMEOUT_MS);

            String endpoint = TestSupport.inprocEndpoint("pair-send-wakeup");
            receiver.bind(endpoint);
            sender.connect(endpoint);
            TestSupport.sleepMs(30);

            int filled = fillUntilBackpressure(sender, PAYLOAD);
            assertTrue(filled > 0);

            ExecutorService executor = Executors.newSingleThreadExecutor();
            try {
                Future<Integer> sendFuture = executor.submit(
                    () -> sender.send(PAYLOAD, SendFlag.NONE));

                TestSupport.sleepMs(100);
                for (int i = 0; i < LWM - 1; i++) {
                    assertEquals(MSG_SIZE, receiver.recv(MSG_SIZE,
                        ReceiveFlag.NONE).length);
                }

                assertThrows(TimeoutException.class,
                    () -> sendFuture.get(120, TimeUnit.MILLISECONDS));

                assertEquals(MSG_SIZE, receiver.recv(MSG_SIZE,
                    ReceiveFlag.NONE).length);
                int rc = sendFuture.get(1500, TimeUnit.MILLISECONDS);
                assertEquals(MSG_SIZE, rc);
            } finally {
                executor.shutdownNow();
            }
        }
    }

    @Test
    public void testPairBlockingSendTimesOutWithoutReceiverReads() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket receiver = new Socket(ctx, SocketType.PAIR);
             Socket sender = new Socket(ctx, SocketType.PAIR)) {
            configurePairSocket(receiver);
            configurePairSocket(sender);
            sender.setSockOpt(SocketOption.SNDTIMEO, SEND_TIMEOUT_MS);

            String endpoint = TestSupport.inprocEndpoint("pair-send-timeout");
            receiver.bind(endpoint);
            sender.connect(endpoint);
            TestSupport.sleepMs(30);

            int filled = fillUntilBackpressure(sender, PAYLOAD);
            assertTrue(filled > 0);

            long start = System.nanoTime();
            assertThrows(RuntimeException.class,
                () -> sender.send(PAYLOAD, SendFlag.NONE));
            long elapsedMs = (System.nanoTime() - start) / 1_000_000L;
            assertTrue(elapsedMs >= SEND_TIMEOUT_MS - 250,
                "elapsedMs=" + elapsedMs);
        }
    }

    private static void configurePairSocket(Socket socket) {
        socket.setSockOpt(SocketOption.SNDHWM, SOCKET_HWM);
        socket.setSockOpt(SocketOption.RCVHWM, SOCKET_HWM);
        socket.setSockOpt(SocketOption.LINGER, 0);
    }

    private static int fillUntilBackpressure(Socket sender, byte[] payload) {
        int sent = 0;
        long deadline = System.currentTimeMillis() + 4000;
        while (System.currentTimeMillis() < deadline) {
            try {
                sender.send(payload, SendFlag.DONTWAIT);
                sent++;
            } catch (RuntimeException ex) {
                if (sent > 0)
                    return sent;
            }
        }
        return -1;
    }

    private static byte[] filledPayload(int size, byte fill) {
        byte[] out = new byte[size];
        for (int i = 0; i < out.length; i++) {
            out[i] = fill;
        }
        return out;
    }
}
