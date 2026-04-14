package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.TestSupport;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

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
    public void borrowedWrapApisAreNotPublic() {
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
