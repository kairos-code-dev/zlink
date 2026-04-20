package dev.kairoscode.zlink.netty;

import dev.kairoscode.zlink.Message;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

public class NettyAdapterContractTest {

    @Test
    public void copyOfByteBufDoesNotMutateReaderIndex() {
        ByteBuf source = Unpooled.directBuffer();
        source.writeBytes("alpha".getBytes(StandardCharsets.UTF_8));
        source.readerIndex(1);

        try (Message msg = NettyMessageAdapter.copyOf(source)) {
            assertEquals(1, source.readerIndex());
            assertArrayEquals("lpha".getBytes(StandardCharsets.UTF_8),
                msg.toByteArray());
        } finally {
            source.release();
        }
    }

    @Test
    public void wrapByteBufDirectZeroCopy() {
        ByteBuf source = Unpooled.directBuffer();
        source.writeBytes("beta".getBytes(StandardCharsets.UTF_8));

        try (Message msg = NettyMessageAdapter.wrap(source)) {
            assertArrayEquals("beta".getBytes(StandardCharsets.UTF_8),
                msg.toByteArray());
        } finally {
            source.release();
        }
    }

    @Test
    public void copyToByteBufWrites() {
        ByteBuf dest = Unpooled.buffer(16);
        try (Message msg = Message.copyOf("gamma".getBytes(StandardCharsets.UTF_8))) {
            int written = NettyMessageAdapter.copyTo(msg, dest);
            assertEquals(5, written);
            byte[] actual = new byte[written];
            dest.getBytes(0, actual);
            assertArrayEquals("gamma".getBytes(StandardCharsets.UTF_8), actual);
        } finally {
            dest.release();
        }
    }

    @Test
    public void wrapDirectNotOnMessageClass() {
        assertFalse(hasPublicMethod(Message.class, "wrapDirect", ByteBuf.class));
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameterTypes) {
        try {
            Method method = type.getMethod(name, parameterTypes);
            return method != null;
        } catch (NoSuchMethodException ex) {
            return false;
        }
    }
}
