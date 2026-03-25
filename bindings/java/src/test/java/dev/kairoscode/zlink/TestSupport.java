package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.spot.Spot;
import org.junit.jupiter.api.Assumptions;

import java.io.IOException;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.util.function.BooleanSupplier;
import java.util.function.Supplier;

public final class TestSupport {
    public static final int POLL_INTERVAL_MS = 10;
    public static final int SHORT_TIMEOUT_MS = 2000;
    public static final int DEFAULT_TIMEOUT_MS = 5000;

    private TestSupport() {
    }

    public static void assumeNative() {
        try {
            ZlinkVersion.get();
        } catch (Throwable t) {
            Assumptions.assumeTrue(false,
                "zlink native library not found: " + t.getMessage());
        }
    }

    public static void sleepMs(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(e);
        }
    }

    public static String inprocEndpoint(String prefix) {
        return "inproc://" + prefix + "-" + System.nanoTime();
    }

    public static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + randomPort();
    }

    public static String wsEndpoint() {
        return "ws://127.0.0.1:" + randomPort();
    }

    public static byte[] recvWithTimeout(Socket socket, int size,
                                         int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return socket.recv(size, ReceiveFlag.DONTWAIT);
            } catch (RuntimeException ex) {
                last = ex;
                sleepMs(POLL_INTERVAL_MS);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("recv timeout");
    }

    public static Spot.SpotMessage spotRecvWithTimeout(Spot spot,
                                                       int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return spot.recv(ReceiveFlag.DONTWAIT);
            } catch (RuntimeException ex) {
                last = ex;
                sleepMs(POLL_INTERVAL_MS);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("spot recv timeout");
    }

    public static Receiver.ReceiverMessages receiverRecvWithTimeout(
      Receiver receiver, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return receiver.recv(ReceiveFlag.DONTWAIT);
            } catch (RuntimeException ex) {
                last = ex;
                sleepMs(POLL_INTERVAL_MS);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("receiver recv timeout");
    }

    public static <T> T waitFor(Supplier<T> supplier, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return supplier.get();
            } catch (RuntimeException ex) {
                last = ex;
                sleepMs(POLL_INTERVAL_MS);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("wait timeout");
    }

    public static void waitUntil(BooleanSupplier condition, int timeoutMs,
                                 String failMessage) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                if (condition.getAsBoolean())
                    return;
            } catch (RuntimeException ignored) {
            }
            sleepMs(POLL_INTERVAL_MS);
        }
        throw new AssertionError(failMessage);
    }

    public static void assertNoMessage(Socket socket, int size, int quietMs) {
        long deadline = System.currentTimeMillis() + quietMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                socket.recv(size, ReceiveFlag.DONTWAIT);
                throw new AssertionError("unexpected message");
            } catch (RuntimeException ignored) {
                sleepMs(POLL_INTERVAL_MS);
            }
        }
    }

    public static String bytesToString(byte[] bytes) {
        int len = bytes.length;
        while (len > 0 && bytes[len - 1] == 0)
            len--;
        return new String(bytes, 0, len, StandardCharsets.UTF_8);
    }

    private static int randomPort() {
        try (ServerSocket server = new ServerSocket(0)) {
            server.setReuseAddress(true);
            return server.getLocalPort();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}
