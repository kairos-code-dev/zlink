package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.SocketTimeoutException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestStreamSendBlockingWakeupPortedTest {
    private static final int STREAM_HWM = 10;
    private static final int SEND_TIMEOUT_MS = 1000;
    private static final int WAKEUP_SEND_TIMEOUT_MS = 5000;
    private static final int PROBE_TIMEOUT_MS = 250;
    private static final int LINGER_MS = 0;
    private static final int SOCKET_BUF_BYTES = 4096;
    private static final int PAYLOAD_SIZE = 4096;
    private static final int FILL_DEADLINE_MS = 5000;

    @Test
    public void testStreamQueueReopensAfterPeerReads() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.STREAM)) {
            configureStreamSocket(server);
            server.setSockOpt(SocketOption.SNDTIMEO, WAKEUP_SEND_TIMEOUT_MS);

            String endpoint = TestSupport.tcpEndpoint();
            int port = parsePort(endpoint);
            server.bind(endpoint);

            try (java.net.Socket raw = new java.net.Socket("127.0.0.1", port)) {
                raw.setReceiveBufferSize(SOCKET_BUF_BYTES);
                raw.setSendBufferSize(SOCKET_BUF_BYTES);
                raw.setSoTimeout(200);
                byte[] rid = establishStreamRoute(server, raw);
                fillStreamSendQueueUntilHwm(server, rid);

                byte[] payload = filledPayload(PAYLOAD_SIZE, (byte) 0x33);
                server.setSockOpt(SocketOption.SNDTIMEO, PROBE_TIMEOUT_MS);
                assertThrows(RuntimeException.class,
                    () -> server.streamSend(rid, payload, SendFlag.NONE));

                final int drainTarget = PAYLOAD_SIZE * ((STREAM_HWM + 1) / 2 + 2);
                drainRaw(raw.getInputStream(), drainTarget, 1000);

                boolean reopened = false;
                long deadline = System.currentTimeMillis() + 1500;
                byte[] drainBuf = new byte[64 * 1024];
                while (System.currentTimeMillis() < deadline) {
                    try {
                        int rc = server.streamSend(rid, payload, SendFlag.DONTWAIT);
                        if (rc == payload.length) {
                            reopened = true;
                            break;
                        }
                    } catch (RuntimeException ex) {
                        // Queue still full.
                    }
                    try {
                        raw.getInputStream().read(drainBuf);
                    } catch (SocketTimeoutException ignored) {
                    }
                }
                assertTrue(reopened, "stream queue did not reopen");
            }
        }
    }

    @Test
    public void testStreamBlockingSendTimesOutWithoutPeerReads() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.STREAM)) {
            configureStreamSocket(server);
            String endpoint = TestSupport.tcpEndpoint();
            int port = parsePort(endpoint);
            server.bind(endpoint);

            try (java.net.Socket raw = new java.net.Socket("127.0.0.1", port)) {
                raw.setReceiveBufferSize(SOCKET_BUF_BYTES);
                raw.setSendBufferSize(SOCKET_BUF_BYTES);
                raw.setSoTimeout(200);
                byte[] rid = establishStreamRoute(server, raw);
                fillStreamSendQueueUntilHwm(server, rid);

                byte[] payload = filledPayload(PAYLOAD_SIZE, (byte) 0x44);
                long start = System.nanoTime();
                assertThrows(RuntimeException.class,
                    () -> server.streamSend(rid, payload, SendFlag.NONE));
                long elapsedMs = (System.nanoTime() - start) / 1_000_000L;
                assertTrue(elapsedMs >= SEND_TIMEOUT_MS - 250,
                    "elapsedMs=" + elapsedMs);
            }
        }
    }

    private static void configureStreamSocket(Socket socket) {
        socket.setSockOpt(SocketOption.LINGER, LINGER_MS);
        socket.setSockOpt(SocketOption.SNDHWM, STREAM_HWM);
        socket.setSockOpt(SocketOption.RCVHWM, STREAM_HWM);
        socket.setSockOpt(SocketOption.SNDBUF, SOCKET_BUF_BYTES);
        socket.setSockOpt(SocketOption.RCVBUF, SOCKET_BUF_BYTES);
        socket.setSockOpt(SocketOption.SNDTIMEO, SEND_TIMEOUT_MS);
    }

    private static byte[] establishStreamRoute(Socket server, java.net.Socket raw)
      throws Exception {
        OutputStream out = raw.getOutputStream();
        out.write(0xA5);
        out.flush();

        byte[] routingId = server.recv(16, ReceiveFlag.NONE);
        assertEquals(4, routingId.length);
        byte[] payload = server.recv(16, ReceiveFlag.NONE);
        assertEquals(1, payload.length);
        assertEquals((byte) 0xA5, payload[0]);
        return routingId;
    }

    private static void fillStreamSendQueueUntilHwm(Socket server, byte[] rid) {
        byte[] payload = filledPayload(PAYLOAD_SIZE, (byte) 0x5A);
        int sent = 0;
        long noSuccessSince = System.nanoTime();
        long deadline = System.currentTimeMillis() + FILL_DEADLINE_MS;
        boolean reachedFull = false;
        while (System.currentTimeMillis() < deadline) {
            try {
                int rc = server.streamSend(rid, payload, SendFlag.DONTWAIT);
                if (rc == payload.length) {
                    sent++;
                    noSuccessSince = System.nanoTime();
                    continue;
                }
            } catch (RuntimeException ex) {
            }

            long fullWindowMs = (System.nanoTime() - noSuccessSince) / 1_000_000L;
            if (fullWindowMs >= 120) {
                reachedFull = true;
                break;
            }
            TestSupport.sleepMs(1);
        }

        assertTrue(sent > 0, "stream queue was never filled");
        assertTrue(reachedFull, "stream queue never reached stable full state");
    }

    private static int drainRaw(InputStream in, int target, int timeoutMs)
      throws Exception {
        int drained = 0;
        long deadline = System.currentTimeMillis() + timeoutMs;
        byte[] buf = new byte[64 * 1024];
        while (System.currentTimeMillis() < deadline && drained < target) {
            try {
                int n = in.read(buf);
                if (n > 0) {
                    drained += n;
                } else {
                    break;
                }
            } catch (SocketTimeoutException ignored) {
            }
        }
        return drained;
    }

    private static byte[] filledPayload(int size, byte value) {
        byte[] out = new byte[size];
        for (int i = 0; i < out.length; i++) {
            out[i] = value;
        }
        return out;
    }

    private static int parsePort(String endpoint) {
        int idx = endpoint.lastIndexOf(':');
        if (idx <= 0 || idx == endpoint.length() - 1)
            throw new IllegalArgumentException("invalid endpoint: " + endpoint);
        return Integer.parseInt(endpoint.substring(idx + 1));
    }
}
