package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestHeartbeatsPortedTest {
    @Test
    public void testHandshakeTimeoutProducesDisconnectedEvent() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.ROUTER)) {
            server.setSockOpt(SocketOption.HANDSHAKE_IVL, 100);
            server.setSockOpt(SocketOption.LINGER, 0);

            String endpoint = TestSupport.tcpEndpoint();
            int port = parsePort(endpoint);
            server.bind(endpoint);

            int events = MonitorEventType.combine(MonitorEventType.ACCEPTED,
                MonitorEventType.DISCONNECTED);

            try (MonitorSocket mon = server.monitorOpen(events);
                 java.net.Socket raw = new java.net.Socket("127.0.0.1", port)) {
                MonitorEvent accepted = waitForEvent(mon,
                    MonitorEventType.ACCEPTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                assertEquals(MonitorEventType.ACCEPTED.getValue(),
                    accepted.event());

                MonitorEvent disconnected = waitForEvent(mon,
                    MonitorEventType.DISCONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                assertEquals(MonitorEventType.DISCONNECTED.getValue(),
                    disconnected.event());
            }
        }
    }

    private static MonitorEvent waitForEvent(MonitorSocket mon,
                                             long expected,
                                             int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                MonitorEvent event = mon.recv(ReceiveFlag.DONTWAIT);
                if (event.event() == expected)
                    return event;
            } catch (RuntimeException ex) {
                last = ex;
                TestSupport.sleepMs(TestSupport.POLL_INTERVAL_MS);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("monitor timeout for event=" + expected);
    }

    private static int parsePort(String endpoint) {
        int idx = endpoint.lastIndexOf(':');
        if (idx <= 0 || idx == endpoint.length() - 1)
            throw new IllegalArgumentException("invalid endpoint: " + endpoint);
        return Integer.parseInt(endpoint.substring(idx + 1));
    }
}
