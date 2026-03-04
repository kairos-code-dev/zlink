package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class CoreCtxOptionsPortedTest {
    @Test
    public void testCtxOptionsAndShutdown() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            assertTrue(ctx.getOption(ContextOption.MAX_SOCKETS) > 0);
            assertTrue(ctx.getOption(ContextOption.SOCKET_LIMIT) > 0);
            assertTrue(ctx.getOption(ContextOption.IO_THREADS) > 0);

            ctx.setOption(ContextOption.IO_THREADS, 2);
            assertEquals(2, ctx.getOption(ContextOption.IO_THREADS));

            ctx.setOption(ContextOption.THREAD_NAME_PREFIX, 1234);
            assertEquals(1234, ctx.getOption(ContextOption.THREAD_NAME_PREFIX));

            // Some scheduler options can return -1 on unsupported platforms.
            assertTrue(ctx.getOption(ContextOption.THREAD_SCHED_POLICY) >= -1);

            ctx.shutdown();
            assertThrows(RuntimeException.class,
                () -> new Socket(ctx, SocketType.PAIR));
        }
    }
}
