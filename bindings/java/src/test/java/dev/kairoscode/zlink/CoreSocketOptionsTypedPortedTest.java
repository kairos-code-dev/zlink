package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class CoreSocketOptionsTypedPortedTest {
    @Test
    public void testTypedIntRoundTrip() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.PAIR)) {
            socket.setOption(SocketOptions.SNDHWM, 321);
            int actual = socket.getOption(SocketOptions.SNDHWM);
            assertEquals(321, actual);
        }
    }

    @Test
    public void testTypedStringRoundTrip() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.DEALER)) {
            socket.setOption(SocketOptions.ROUTING_ID, "typed-routing-id");
            String actual = socket.getOption(SocketOptions.ROUTING_ID);
            assertEquals("typed-routing-id", actual);
        }
    }

    @Test
    public void testTypedBytesRoundTrip() {
        TestSupport.assumeNative();

        byte[] expected = "typed-bytes-id".getBytes(StandardCharsets.UTF_8);
        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.DEALER)) {
            socket.setOption(SocketOptions.ROUTING_ID_BYTES, expected);
            byte[] actual = socket.getOption(SocketOptions.ROUTING_ID_BYTES);
            assertArrayEquals(expected, actual);
        }
    }

    @Test
    public void testWriteOnlyOptionRejectsGet() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.SUB)) {
            socket.setOption(SocketOptions.SUBSCRIBE, "topic");
            assertThrows(IllegalArgumentException.class,
                () -> socket.getOption(SocketOptions.SUBSCRIBE));
        }
    }

    @Test
    public void testOption98RejectsTlsKeyOnXpub() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.XPUB)) {
            assertThrows(IllegalArgumentException.class,
                () -> socket.setOption(SocketOptions.TLS_VERIFY, 1));
        }
    }

    @Test
    public void testOption98RejectsXpubKeyOnNonXpub() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket socket = new Socket(ctx, SocketType.PAIR)) {
            assertThrows(IllegalArgumentException.class,
                () -> socket.setOption(SocketOptions.XPUB_MANUAL_LAST_VALUE, 1));
        }
    }
}
