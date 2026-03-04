package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestConnectResolvePortedTest {
    @Test
    public void testHostnameIpv4() {
        withPubSocket(sock ->
            assertDoesNotThrow(() -> sock.connect("tcp://localhost:1234")));
    }

    @Test
    public void testLoopbackIpv6() {
        withPubSocket(sock ->
            assertDoesNotThrow(() -> sock.connect("tcp://[::1]:1234")));
    }

    @Test
    public void testInvalidServiceFails() {
        withPubSocket(sock -> assertThrows(RuntimeException.class,
            () -> sock.connect("tcp://localhost:invalid")));
    }

    @Test
    public void testHostnameWithSpacesFails() {
        withPubSocket(sock -> assertThrows(RuntimeException.class,
            () -> sock.connect("tcp://in val id:1234")));
    }

    @Test
    public void testNoHostnameFails() {
        withPubSocket(sock -> assertThrows(RuntimeException.class,
            () -> sock.connect("tcp://")));
    }

    @Test
    public void testInvalidProtoFails() {
        withPubSocket(sock -> assertThrows(RuntimeException.class,
            () -> sock.connect("invalid://localhost:1234")));
    }

    private static void withPubSocket(SocketAction action) {
        TestSupport.assumeNative();
        try (Context ctx = new Context();
             Socket sock = new Socket(ctx, SocketType.PUB)) {
            action.run(sock);
        }
    }

    @FunctionalInterface
    private interface SocketAction {
        void run(Socket socket);
    }
}
