package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class CoreSocketSpanPortedTest {
    @Test
    public void testPairSendRecvWithArrayRange() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("span-array");
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
    public void testPairSendRecvWithByteBuffer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("span-buffer");
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
    public void testPairSendWithDirectBuffer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("send-direct");
            a.bind(endpoint);
            b.connect(endpoint);

            ByteBuffer send = ByteBuffer.allocateDirect(32);
            send.put("const-frame".getBytes(StandardCharsets.UTF_8));
            send.flip();

            int sent = b.send(send, SendFlag.NONE);
            assertEquals(11, sent);
            assertEquals(11, send.position());

            byte[] out = a.recv(32, ReceiveFlag.NONE);
            assertArrayEquals("const-frame".getBytes(StandardCharsets.UTF_8), out);
        }
    }

    @Test
    public void testSendHeapBuffer() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("send-heap");
            a.bind(endpoint);
            b.connect(endpoint);

            ByteBuffer heap = ByteBuffer.wrap("heap".getBytes(StandardCharsets.UTF_8));
            int sent = b.send(heap, SendFlag.NONE);
            assertEquals(4, sent);
            assertEquals(4, heap.position());

            byte[] out = a.recv(16, ReceiveFlag.NONE);
            assertArrayEquals("heap".getBytes(StandardCharsets.UTF_8), out);
        }
    }

    @Test
    public void testPairSendRecvWithMemorySegmentSpan() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR);
             Arena arena = Arena.ofConfined()) {
            String endpoint = TestSupport.inprocEndpoint("span-segment");
            a.bind(endpoint);
            b.connect(endpoint);

            byte[] payload = "segment".getBytes(StandardCharsets.UTF_8);
            MemorySegment src = arena.allocate(payload.length);
            MemorySegment.copy(MemorySegment.ofArray(payload), 0, src, 0,
                payload.length);

            int sent = b.send(ByteSpan.of(src), SendFlag.NONE);
            assertEquals(payload.length, sent);

            byte[] recv = new byte[16];
            int received = a.recv(ByteSpan.of(recv, 0, payload.length),
                ReceiveFlag.NONE);
            assertEquals(payload.length, received);
            byte[] out = Arrays.copyOf(recv, payload.length);
            assertArrayEquals(payload, out);
        }
    }
}
