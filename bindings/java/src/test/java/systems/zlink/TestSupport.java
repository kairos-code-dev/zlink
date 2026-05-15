package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import org.junit.jupiter.api.Assertions;

import java.io.IOException;
import java.net.ServerSocket;
import java.util.function.BooleanSupplier;

public final class TestSupport {
    public static final int DEFAULT_TIMEOUT_MS = 5000;

    private TestSupport() {
    }

    public static void assumeNative() {
        try {
            Zlink.version();
        } catch (Throwable t) {
            Assertions.fail(
                "zlink native library not found: " + t.getMessage());
        }
    }

    public static String inprocEndpoint(String prefix) {
        return "inproc://" + prefix + "-" + System.nanoTime();
    }

    public static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + randomPort();
    }

    public static MonitorEvent awaitMonitorEvent(MonitorSocket monitor,
                                                 MonitorEventType eventType) {
        while (true) {
            MonitorEvent event = monitor.recv();
            if (event.event() == eventType) {
                return event;
            }
        }
    }

    public static void awaitCondition(BooleanSupplier condition) {
        awaitCondition(condition, DEFAULT_TIMEOUT_MS);
    }

    public static void awaitCondition(BooleanSupplier condition, long timeoutMs) {
        long deadline = System.nanoTime() + timeoutMs * 1_000_000L;
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return;
            }
            try {
                Thread.sleep(10L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(e);
            }
        }
        throw new AssertionError("condition did not become true within " + timeoutMs + "ms");
    }

    public static void allowTcpRequestReplyCallbackHandshakeToSettle() {
        try {
            // CONNECTION_READY can arrive before the ROUTER request/reply
            // callback path finishes its peer routing-id handshake.
            Thread.sleep(100L);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(e);
        }
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
