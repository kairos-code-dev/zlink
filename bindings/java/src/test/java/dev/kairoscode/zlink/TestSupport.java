package dev.kairoscode.zlink;

import org.junit.jupiter.api.Assumptions;

import java.io.IOException;
import java.net.ServerSocket;

public final class TestSupport {
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
            if (event.event() == eventType.getValue()) {
                return event;
            }
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
