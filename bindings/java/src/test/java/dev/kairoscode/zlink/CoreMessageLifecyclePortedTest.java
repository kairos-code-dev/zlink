package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

public class CoreMessageLifecyclePortedTest {
    @Test
    public void testMessageCloseIsIdempotent() {
        TestSupport.assumeNative();

        Message message = new Message();
        assertDoesNotThrow(message::close);
        assertDoesNotThrow(message::close);
    }
}
