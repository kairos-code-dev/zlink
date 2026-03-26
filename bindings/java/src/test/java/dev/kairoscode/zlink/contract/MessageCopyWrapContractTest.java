package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.TestSupport;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

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
    public void wrapDirectByteBufferDoesNotMutateSourceCursorAndKeepsBorrowedView() {
        TestSupport.assumeNative();

        ByteBuffer source = ByteBuffer.allocateDirect(5);
        source.put("alpha".getBytes(StandardCharsets.UTF_8));
        source.flip();
        source.position(1);

        try (Message msg = Message.wrapDirect(source)) {
            assertEquals(1, source.position());
            source.put(1, (byte) 'Z');
            assertArrayEquals("Zpha".getBytes(StandardCharsets.UTF_8),
                msg.toByteArray());
        }
    }
}
