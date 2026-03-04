package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

public class TestBindSrcAddressPortedTest {
    @Test
    public void testBindSourceAddressSyntaxAccepted() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket sock = new Socket(ctx, SocketType.PUB)) {
            assertDoesNotThrow(
                () -> sock.connect("tcp://127.0.0.1:0;localhost:1234"));
            assertDoesNotThrow(
                () -> sock.connect("tcp://localhost:5555;localhost:1235"));
            assertDoesNotThrow(
                () -> sock.connect("tcp://lo:5555;localhost:1235"));
        }
    }
}
