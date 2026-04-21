package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.TestSupport;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class MessageCopyWrapContractTest {
    @Test
    public void copyOfByteBufferDoesNotMutateSourceCursor() {
        TestSupport.assumeNative();

        ByteBuffer source = ByteBuffer.wrap("alpha".getBytes(StandardCharsets.UTF_8));
        source.position(1);

        try (Message msg = Message.copyOf(source)) {
            assertEquals(1, source.position());
            assertArrayEquals("lpha".getBytes(StandardCharsets.UTF_8),
                msg.toByteArray());
        }
    }

    @Test
    public void wrapDirectByteBufferIsNotPublic() {
        assertFalse(hasPublicMethod(Message.class, "wrapDirect", ByteBuffer.class));
    }

    @Test
    public void moveTransfersPayloadToNewMessage() {
        TestSupport.assumeNative();

        try (Message source = Message.copyOfUtf8("alpha")) {
            try (Message moved = source.move()) {
                assertArrayEquals("alpha".getBytes(StandardCharsets.UTF_8),
                    moved.toByteArray());
                assertTrue(source.empty());
            }
        }
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
