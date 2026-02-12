package dev.kairoscode.zlink;

import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class SocketSpanTest {
    @Test
    public void pairSendRecvWithArrayRange() {
        assumeNative();
        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = "inproc://java-span-array-" + System.nanoTime();
            a.bind(endpoint);
            b.connect(endpoint);

            byte[] src = "xxpayloadyy".getBytes(StandardCharsets.UTF_8);
            ByteSpan sendSpan = ByteSpan.of(src, 2, 7);
            int sent = b.send(sendSpan, SendFlag.NONE);
            assertEquals(7, sent);

            byte[] dst = new byte[16];
            ByteSpan recvSpan = ByteSpan.of(dst, 4, 8);
            int received = a.recv(recvSpan, ReceiveFlag.NONE);
            assertEquals(7, received);
            byte[] out = Arrays.copyOfRange(dst, 4, 11);
            assertArrayEquals("payload".getBytes(StandardCharsets.UTF_8), out);
        }
    }

    @Test
    public void pairSendRecvWithByteBuffer() {
        assumeNative();
        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = "inproc://java-span-buffer-" + System.nanoTime();
            a.bind(endpoint);
            b.connect(endpoint);

            ByteBuffer send = ByteBuffer.allocateDirect(32);
            send.put("buffer-data".getBytes(StandardCharsets.UTF_8));
            send.flip();
            int sent = b.send(send, SendFlag.NONE);
            assertEquals(11, sent);
            assertEquals(11, send.position());

            ByteBuffer recv = ByteBuffer.allocateDirect(32);
            int received = a.recv(recv, ReceiveFlag.NONE);
            assertEquals(11, received);
            assertEquals(11, recv.position());

            recv.flip();
            byte[] out = new byte[11];
            recv.get(out);
            assertArrayEquals("buffer-data".getBytes(StandardCharsets.UTF_8), out);
        }
    }

    @Test
    public void pairSendConstWithDirectBuffer() {
        assumeNative();
        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = "inproc://java-send-const-" + System.nanoTime();
            a.bind(endpoint);
            b.connect(endpoint);

            ByteBuffer send = ByteBuffer.allocateDirect(32);
            send.put("const-frame".getBytes(StandardCharsets.UTF_8));
            send.flip();

            int sent = b.sendConst(send, SendFlag.NONE);
            assertEquals(11, sent);
            assertEquals(11, send.position());

            byte[] out = a.recv(32, ReceiveFlag.NONE);
            assertArrayEquals("const-frame".getBytes(StandardCharsets.UTF_8), out);
        }
    }

    @Test
    public void sendConstRejectsHeapBuffer() {
        assumeNative();
        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.PAIR)) {
            ByteBuffer heap = ByteBuffer.wrap("heap".getBytes(StandardCharsets.UTF_8));
            assertThrows(IllegalArgumentException.class,
                () -> socket.sendConst(heap, SendFlag.NONE));
        }
    }

    @Test
    public void pairSendRecvWithMemorySegmentSpan() {
        assumeNative();
        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR);
             Arena arena = Arena.ofConfined()) {
            String endpoint = "inproc://java-span-segment-" + System.nanoTime();
            a.bind(endpoint);
            b.connect(endpoint);

            byte[] payload = "segment".getBytes(StandardCharsets.UTF_8);
            MemorySegment src = arena.allocate(payload.length);
            MemorySegment.copy(MemorySegment.ofArray(payload), 0, src, 0, payload.length);

            int sent = b.send(ByteSpan.of(src), SendFlag.NONE);
            assertEquals(payload.length, sent);

            byte[] recv = new byte[16];
            int received = a.recv(ByteSpan.of(recv, 0, payload.length), ReceiveFlag.NONE);
            assertEquals(payload.length, received);
            byte[] out = Arrays.copyOf(recv, payload.length);
            assertArrayEquals(payload, out);
        }
    }

    private static void assumeNative() {
        try {
            ZlinkVersion.get();
        } catch (Throwable e) {
            Assumptions.assumeTrue(false,
                "zlink native library not found: " + e.getMessage());
        }
    }
}
