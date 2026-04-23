package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PerfStreamHooks;
import dev.kairoscode.zlink.StreamSocket;
import java.io.InputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PerfMultiStreamRegressionTest {
    @Test
    void framedPacketEchoReturnsOriginalWireBytes() throws Exception {
        CountDownLatch echoed = new CountDownLatch(1);
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        byte[] header = "hello-header".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] body = new byte[64];
        java.util.Arrays.fill(body, (byte) 'x');
        byte[] frame = buildFrame(header, body);

        int port = freePort();
        String endpoint = "tcp://127.0.0.1:" + port;

        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx)) {
            server.options().notify(true);
            server.bind(endpoint);
            PerfStreamHooks.attachFramedPacketHandler(server,
                (routingId, recvHeader, recvBody) -> {
                    try {
                        PerfStreamHooks.sendFramedPacket(server, routingId,
                            recvHeader, recvBody,
                            dev.kairoscode.zlink.SendFlags.NONE);
                        echoed.countDown();
                        return 0;
                    } catch (Throwable t) {
                        callbackError.compareAndSet(null, t);
                        return -1;
                    }
                });

            try (Socket client = new Socket("127.0.0.1", port)) {
                client.setSoTimeout((int) Duration.ofSeconds(5).toMillis());
                client.getOutputStream().write(frame);
                client.getOutputStream().flush();
                byte[] echoedFrame = readExact(client.getInputStream(), frame.length);
                assertArrayEquals(frame, echoedFrame);
            }

            assertTrue(echoed.await(5, TimeUnit.SECONDS),
                "framed stream echo timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
        }
    }

    private static byte[] buildFrame(byte[] header, byte[] body) {
        byte[] frame = new byte[6 + header.length + body.length];
        frame[0] = (byte) ((header.length >>> 8) & 0xFF);
        frame[1] = (byte) (header.length & 0xFF);
        frame[2] = (byte) ((body.length >>> 24) & 0xFF);
        frame[3] = (byte) ((body.length >>> 16) & 0xFF);
        frame[4] = (byte) ((body.length >>> 8) & 0xFF);
        frame[5] = (byte) (body.length & 0xFF);
        System.arraycopy(header, 0, frame, 6, header.length);
        System.arraycopy(body, 0, frame, 6 + header.length, body.length);
        return frame;
    }

    private static byte[] readExact(InputStream input, int length) throws Exception {
        byte[] data = input.readNBytes(length);
        assertTrue(data.length == length,
            "expected " + length + " bytes but received " + data.length);
        return data;
    }

    private static int freePort() throws Exception {
        try (ServerSocket socket = new ServerSocket(0)) {
            socket.setReuseAddress(true);
            return socket.getLocalPort();
        }
    }
}
