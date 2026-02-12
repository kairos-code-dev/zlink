package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class NettyByteBufTest {
    @Test
    public void wrapNettyUsesMethodHandleDelegation() {
        FakeNettyByteBuf delegate = new FakeNettyByteBuf(32);
        delegate.writerIndex(8);
        ByteBuf byteBuf = ByteBuf.wrapNetty(delegate);

        assertEquals(8, byteBuf.readableBytes());
        assertEquals(24, byteBuf.writableBytes());
        assertEquals(0, byteBuf.readerIndex());
        assertEquals(8, byteBuf.writerIndex());

        byteBuf.advanceReader(3);
        byteBuf.advanceWriter(2);

        assertEquals(3, byteBuf.readerIndex());
        assertEquals(10, byteBuf.writerIndex());

        ByteBuffer nio = byteBuf.nioBuffer();
        assertEquals(3, nio.position());
        assertEquals(10, nio.limit());
    }

    @Test
    public void wrapNettyRejectsUnknownType() {
        assertThrows(IllegalArgumentException.class,
            () -> ByteBuf.wrapNetty(new Object()));
    }

    public static final class FakeNettyByteBuf {
        private final ByteBuffer buffer;
        private int readerIndex;
        private int writerIndex;

        public FakeNettyByteBuf(int capacity) {
            this.buffer = ByteBuffer.allocateDirect(capacity);
            this.readerIndex = 0;
            this.writerIndex = 0;
        }

        public int readableBytes() {
            return writerIndex - readerIndex;
        }

        public int writableBytes() {
            return buffer.capacity() - writerIndex;
        }

        public int readerIndex() {
            return readerIndex;
        }

        public int writerIndex() {
            return writerIndex;
        }

        public FakeNettyByteBuf readerIndex(int index) {
            if (index < 0 || index > writerIndex)
                throw new IndexOutOfBoundsException("readerIndex");
            readerIndex = index;
            return this;
        }

        public FakeNettyByteBuf writerIndex(int index) {
            if (index < readerIndex || index > buffer.capacity())
                throw new IndexOutOfBoundsException("writerIndex");
            writerIndex = index;
            return this;
        }

        public ByteBuffer nioBuffer() {
            ByteBuffer dup = buffer.duplicate();
            dup.position(readerIndex);
            dup.limit(writerIndex);
            return dup;
        }
    }
}
