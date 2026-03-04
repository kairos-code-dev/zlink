package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertThrows;

public class CoreSocketNullPortedTest {
    @Test
    public void testSocketNullContext() {
        assertThrows(NullPointerException.class,
            () -> new Socket(null, SocketType.PAIR));
    }

    @Test
    public void testSocketOperationsRejectNullInputs() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.PAIR)) {
            assertThrows(NullPointerException.class, () -> socket.bind(null));
            assertThrows(NullPointerException.class, () -> socket.connect(null));
            assertThrows(NullPointerException.class, () -> socket.unbind(null));
            assertThrows(NullPointerException.class,
                () -> socket.disconnect(null));
            assertThrows(NullPointerException.class,
                () -> socket.setSockOpt(SocketOption.SUBSCRIBE, (byte[]) null));
        }
    }
}
