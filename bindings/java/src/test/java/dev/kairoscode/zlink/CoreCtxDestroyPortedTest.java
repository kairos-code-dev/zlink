package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.jupiter.api.Assertions.assertTrue;

public class CoreCtxDestroyPortedTest {
    @Test
    public void testCtxShutdownUnblocksWaitingSocket() {
        TestSupport.assumeNative();

        Context ctx = new Context();
        Socket socket = new Socket(ctx, SocketType.DEALER);
        socket.bind(TestSupport.inprocEndpoint("ctx-shutdown"));

        AtomicBoolean recvExited = new AtomicBoolean(false);
        Thread receiver = new Thread(() -> {
            try {
                socket.recv(16, ReceiveFlag.NONE);
            } catch (RuntimeException ignored) {
            } finally {
                recvExited.set(true);
            }
        });
        receiver.setDaemon(true);
        receiver.start();

        TestSupport.sleepMs(100);
        ctx.shutdown();

        TestSupport.waitUntil(recvExited::get, TestSupport.DEFAULT_TIMEOUT_MS,
            "recv thread did not exit after ctx shutdown");

        socket.close();
        ctx.close();

        assertTrue(recvExited.get());
    }
}
