package dev.kairoscode.zlink;

import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.*;

public class MessageSpanTest {
    @Test
    public void messageFromBytesWithRange() {
        assumeNative();
        byte[] src = "xxhellozz".getBytes(StandardCharsets.UTF_8);
        try (Message msg = Message.fromBytes(src, 2, 5)) {
            assertEquals(5, msg.size());
            assertArrayEquals("hello".getBytes(StandardCharsets.UTF_8), msg.data());
        }
    }

    @Test
    public void messageFromByteBufferAndCopyTo() {
        assumeNative();
        ByteBuffer src = ByteBuffer.allocateDirect(16);
        src.put("world".getBytes(StandardCharsets.UTF_8));
        src.flip();

        try (Message msg = Message.fromByteBuffer(src)) {
            assertEquals(5, src.position());
            ByteBuffer dst = ByteBuffer.allocate(8);
            int copied = msg.copyTo(dst);
            assertEquals(5, copied);
            assertEquals(5, dst.position());
            dst.flip();
            byte[] out = new byte[5];
            dst.get(out);
            assertArrayEquals("world".getBytes(StandardCharsets.UTF_8), out);
        }
    }

    @Test
    public void messageFromMemorySegmentExposesView() {
        assumeNative();
        byte[] expected = new byte[]{1, 2, 3, 4};
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment seg = arena.allocate(expected.length);
            MemorySegment.copy(MemorySegment.ofArray(expected), 0, seg, 0, expected.length);
            try (Message msg = Message.fromMemorySegment(seg)) {
                MemorySegment view = msg.dataSegment();
                assertEquals(expected.length, view.byteSize());
                byte[] out = new byte[expected.length];
                MemorySegment.copy(view, 0, MemorySegment.ofArray(out), 0, out.length);
                assertArrayEquals(expected, out);

                ByteBuffer tooSmall = ByteBuffer.allocate(2);
                assertFalse(msg.tryCopyTo(tooSmall));
            }
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
