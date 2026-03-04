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
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestMonitoringEnhancedPortedTest {
    @Test
    public void testAutoRoutingIdGeneration() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (SocketType type : SocketType.values()) {
                try (Socket sock = new Socket(ctx, type)) {
                    byte[] rid = sock.getSockOptBytes(SocketOption.ROUTING_ID,
                        255);
                    assertEquals(16, rid.length,
                        "socketType=" + type + ", ridLength=" + rid.length);
                }
            }
        }
    }

    @Test
    public void testMonitorReadyAndDisconnected() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.ROUTER);
             Socket client = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);

            int events = MonitorEventType.combine(
                MonitorEventType.ACCEPTED,
                MonitorEventType.CONNECTION_READY,
                MonitorEventType.DISCONNECTED);

            try (MonitorSocket mon = server.monitorOpen(events)) {
                client.connect(endpoint);

                MonitorEvent ready = waitForEvent(mon,
                    MonitorEventType.CONNECTION_READY.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                assertTrue(ready.routingId().length > 0);
                assertTrue(!ready.remoteAddress().isEmpty()
                    || !ready.localAddress().isEmpty());

                client.close();

                MonitorEvent disconnected = waitForEvent(mon,
                    MonitorEventType.DISCONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                assertTrue(!disconnected.remoteAddress().isEmpty()
                    || !disconnected.localAddress().isEmpty());
            }
        }
    }

    private static MonitorEvent waitForEvent(MonitorSocket mon,
                                             long expectedEvent,
                                             int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                MonitorEvent ev = mon.recv(ReceiveFlag.DONTWAIT);
                if (ev.event() == expectedEvent)
                    return ev;
            } catch (RuntimeException ex) {
                last = ex;
                TestSupport.sleepMs(TestSupport.POLL_INTERVAL_MS);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("monitor timeout for event=" + expectedEvent);
    }
}
