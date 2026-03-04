package dev.kairoscode.zlink;

import io.netty.buffer.Unpooled;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class CoreNettyByteBufPortedTest {
    @Test
    public void testSocketNettyByteBufOverloadsExist() {
        assertDoesNotThrow(() -> Socket.class.getMethod("send",
            io.netty.buffer.ByteBuf.class, SendFlag.class));
        assertDoesNotThrow(() -> Socket.class.getMethod("recv",
            io.netty.buffer.ByteBuf.class, ReceiveFlag.class));
    }

    @Test
    public void testWrapNettyUsesRealNettyByteBuf() {
        io.netty.buffer.ByteBuf delegate = Unpooled.buffer(32);
        delegate.writeBytes(new byte[]{1, 2, 3, 4, 5, 6, 7, 8});
        ByteBuf byteBuf = ByteBuf.wrapNetty(delegate);

        assertEquals(8, byteBuf.readableBytes());
        assertEquals(24, byteBuf.writableBytes());
        assertEquals(0, byteBuf.readerIndex());
        assertEquals(8, byteBuf.writerIndex());

        byteBuf.advanceReader(3);
        byteBuf.advanceWriter(2);

        assertEquals(3, byteBuf.readerIndex());
        assertEquals(10, byteBuf.writerIndex());
    }

    @Test
    public void testWrapNettyRejectsUnknownType() {
        assertThrows(IllegalArgumentException.class,
            () -> ByteBuf.wrapNetty(new Object()));
    }
}
