package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestConnectRidStringAliasPortedTest {
    @Test
    public void testStreamConnectRoutingIdRejected() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.STREAM)) {
            socket.setSockOpt(SocketOption.LINGER, 0);

            byte[] alias = "stream-alias".getBytes(StandardCharsets.UTF_8);
            assertThrows(RuntimeException.class,
                () -> socket.setSockOpt(SocketOption.CONNECT_ROUTING_ID, alias));

            byte[] fixedId = new byte[]{'R', 'I', 'D', '1'};
            assertThrows(RuntimeException.class,
                () -> socket.setSockOpt(SocketOption.CONNECT_ROUTING_ID, fixedId));
        }
    }
}
