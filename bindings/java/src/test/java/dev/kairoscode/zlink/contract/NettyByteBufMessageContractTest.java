package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.TestSupport;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class NettyByteBufMessageContractTest {
    @Test
    public void copyOfByteBufDoesNotMutateReaderIndex() {
        TestSupport.assumeNative();

        ByteBuf source = Unpooled.directBuffer();
        source.writeBytes("alpha".getBytes(StandardCharsets.UTF_8));
        source.readerIndex(1);

        try (Message msg = Message.copyOf(source)) {
            assertEquals(1, source.readerIndex());
            assertArrayEquals("lpha".getBytes(StandardCharsets.UTF_8),
                msg.toByteArray());
        } finally {
            source.release();
        }
    }

    @Test
    public void wrapDirectByteBufKeepsBorrowedView() {
        TestSupport.assumeNative();

        ByteBuf source = Unpooled.directBuffer();
        source.writeBytes("alpha".getBytes(StandardCharsets.UTF_8));
        source.readerIndex(1);

        try (Message msg = Message.wrapDirect(source)) {
            assertEquals(1, source.readerIndex());
            source.setByte(1, 'Z');
            assertArrayEquals("Zpha".getBytes(StandardCharsets.UTF_8),
                msg.toByteArray());
        } finally {
            source.release();
        }
    }
}
