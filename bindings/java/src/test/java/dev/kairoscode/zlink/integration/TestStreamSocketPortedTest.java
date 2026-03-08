package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestStreamSocketPortedTest {
    @Test
    public void testStreamAttachRawAndStreamSend() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket stream = new Socket(ctx, SocketType.STREAM)) {
            String endpoint = TestSupport.tcpEndpoint();
            int port = parsePort(endpoint);
            stream.bind(endpoint);

            CountDownLatch called = new CountDownLatch(1);
            AtomicReference<Long> seenRoutingId = new AtomicReference<>();
            AtomicReference<byte[]> seenPayload = new AtomicReference<>();

            stream.attachStreamRaw((routingId, payload) -> {
                try (Message msg = payload) {
                    seenRoutingId.set(routingId);
                    seenPayload.set(msg.data());
                    stream.streamSend(routingId,
                        "ack".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
                }
                called.countDown();
                return 0;
            });

            try (java.net.Socket raw = new java.net.Socket("127.0.0.1", port)) {
                raw.setSoTimeout(TestSupport.DEFAULT_TIMEOUT_MS);
                OutputStream out = raw.getOutputStream();
                InputStream in = raw.getInputStream();

                out.write("hello".getBytes(StandardCharsets.UTF_8));
                out.flush();

                assertTrue(called.await(TestSupport.DEFAULT_TIMEOUT_MS,
                    TimeUnit.MILLISECONDS));

                byte[] ack = in.readNBytes(3);
                assertArrayEquals("ack".getBytes(StandardCharsets.UTF_8), ack);
            }

            assertTrue(seenRoutingId.get() != null);
            assertEquals("hello", new String(seenPayload.get(),
                StandardCharsets.UTF_8));

            byte[] peerRid = stream.streamPeerRoutingIdBytes();
            assertTrue(peerRid == null || peerRid.length > 0);
            Long peerRidValue = stream.streamPeerRoutingId();
            assertTrue(peerRidValue == null || (peerRidValue >= 0
                && peerRidValue <= 0xFFFF_FFFFL));
            stream.detachStream();
        }
    }

    @Test
    public void testStreamSendRejectsOutOfRangeU32RoutingId() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket stream = new Socket(ctx, SocketType.STREAM)) {
            assertThrows(IllegalArgumentException.class,
                () -> stream.streamSend(0x1_0000_0000L, new byte[0],
                    SendFlag.NONE));
        }
    }

    @Test
    public void testStreamAttachLen32beBacklogEcho() throws Exception {
        TestSupport.assumeNative();

        final int frameCount = 1024;
        final byte[] expectedPayload = new byte[128];
        Arrays.fill(expectedPayload, (byte) 'x');

        try (Context ctx = new Context();
             Socket stream = new Socket(ctx, SocketType.STREAM)) {
            String endpoint = TestSupport.tcpEndpoint();
            int port = parsePort(endpoint);
            stream.bind(endpoint);

            CountDownLatch called = new CountDownLatch(frameCount);
            stream.attachStreamLen32be((routingId, packets) -> {
                if (packets == null) {
                    return 0;
                }
                for (Message packet : packets) {
                    if (packet == null) {
                        continue;
                    }
                    int size = packet.size();
                    if (size <= 1) {
                        packet.close();
                        continue;
                    }
                    stream.streamSend(routingId, packet, SendFlag.NONE);
                    called.countDown();
                }
                return 0;
            });

            try (java.net.Socket raw = new java.net.Socket("127.0.0.1", port)) {
                raw.setSoTimeout(TestSupport.DEFAULT_TIMEOUT_MS);
                OutputStream out = raw.getOutputStream();
                InputStream in = raw.getInputStream();

                for (int i = 0; i < frameCount; i++) {
                    writeLen32beFrame(out, expectedPayload);
                }
                out.flush();

                assertTrue(called.await(TestSupport.DEFAULT_TIMEOUT_MS,
                    TimeUnit.MILLISECONDS));
                for (int i = 0; i < frameCount; i++) {
                    byte[] echoed = readLen32beFrame(in);
                    assertArrayEquals(expectedPayload, echoed);
                }
            }

            stream.detachStream();
        }
    }

    private static int parsePort(String endpoint) {
        int idx = endpoint.lastIndexOf(':');
        if (idx <= 0 || idx == endpoint.length() - 1)
            throw new IllegalArgumentException("invalid endpoint: " + endpoint);
        return Integer.parseInt(endpoint.substring(idx + 1));
    }

    private static void writeLen32beFrame(OutputStream out, byte[] payload)
      throws Exception {
        ByteBuffer header = ByteBuffer.allocate(4);
        header.putInt(payload.length);
        out.write(header.array());
        out.write(payload);
    }

    private static byte[] readLen32beFrame(InputStream in) throws Exception {
        byte[] header = in.readNBytes(4);
        assertEquals(4, header.length);
        int size = ByteBuffer.wrap(header).getInt();
        byte[] payload = in.readNBytes(size);
        assertEquals(size, payload.length);
        return payload;
    }
}
