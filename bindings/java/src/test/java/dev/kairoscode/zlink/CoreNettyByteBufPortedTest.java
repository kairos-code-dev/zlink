package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

public class CoreNettyByteBufPortedTest {
    @Test
    public void testSocketNettyByteBufOverloadsExist() {
        assertDoesNotThrow(() -> Socket.class.getMethod("send",
            io.netty.buffer.ByteBuf.class, SendFlag.class));
        assertDoesNotThrow(() -> Socket.class.getMethod("recv",
            io.netty.buffer.ByteBuf.class, ReceiveFlag.class));
    }
}
