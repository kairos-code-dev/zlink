package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestManySocketsPortedTest {
    @Test
    public void testContextMaxSocketsLimit() {
        TestSupport.assumeNative();

        final int maxSockets = 128;
        try (Context ctx = new Context()) {
            ctx.setOption(ContextOption.MAX_SOCKETS, maxSockets);

            List<Socket> sockets = new ArrayList<>();
            try {
                while (true) {
                    sockets.add(new Socket(ctx, SocketType.PAIR));
                }
            } catch (RuntimeException ignored) {
            }

            assertTrue(!sockets.isEmpty());
            assertTrue(sockets.size() <= maxSockets,
                "created sockets=" + sockets.size());
            assertTrue(sockets.size() >= maxSockets - 8,
                "created sockets too low=" + sockets.size());

            for (int i = 0; i < 5; i++) {
                assertThrows(RuntimeException.class,
                    () -> new Socket(ctx, SocketType.PAIR));
            }

            for (Socket socket : sockets) {
                socket.close();
            }
        }
    }
}
