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

import static org.junit.jupiter.api.Assertions.fail;

public class TestReconnectOptionsPortedTest {
    @Test
    public void testReconnectDefaultIntervalDoesNotReconnectImmediately() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.tcpEndpoint();

            sub.setSockOpt(SocketOption.LINGER, 0);
            sub.setSockOpt(SocketOption.RECONNECT_IVL, 60_000);

            Socket pub = new Socket(ctx, SocketType.PUB);
            pub.setSockOpt(SocketOption.LINGER, 0);
            pub.bind(endpoint);

            try (MonitorSocket mon = sub.monitorOpen(MonitorEventType.ALL.getValue())) {
                sub.connect(endpoint);

                waitForEvent(mon, MonitorEventType.CONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                waitForEvent(mon, MonitorEventType.CONNECTION_READY.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);

                pub.close();

                waitForEvent(mon, MonitorEventType.DISCONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                waitForEvent(mon, MonitorEventType.CONNECT_RETRIED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);

                assertNoEvent(mon, MonitorEventType.CONNECTED.getValue(), 1200);
            }
        }
    }

    @Test
    public void testReconnectSucceedsAfterPublisherRebind() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.tcpEndpoint();

            sub.setSockOpt(SocketOption.LINGER, 0);
            sub.setSockOpt(SocketOption.RECONNECT_IVL, 1000);

            Socket pub = new Socket(ctx, SocketType.PUB);
            pub.setSockOpt(SocketOption.LINGER, 0);
            pub.bind(endpoint);

            try (MonitorSocket mon = sub.monitorOpen(MonitorEventType.ALL.getValue())) {
                sub.connect(endpoint);

                waitForEvent(mon, MonitorEventType.CONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                waitForEvent(mon, MonitorEventType.CONNECTION_READY.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);

                pub.close();

                waitForEvent(mon, MonitorEventType.DISCONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                waitForEvent(mon, MonitorEventType.CONNECT_RETRIED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);

                pub = new Socket(ctx, SocketType.PUB);
                pub.setSockOpt(SocketOption.LINGER, 0);
                pub.bind(endpoint);

                waitForEvent(mon, MonitorEventType.CONNECTED.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);
                waitForEvent(mon, MonitorEventType.CONNECTION_READY.getValue(),
                    TestSupport.DEFAULT_TIMEOUT_MS);

                pub.close();
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

    private static void assertNoEvent(MonitorSocket mon, long forbidden,
                                      int quietMs) {
        long deadline = System.currentTimeMillis() + quietMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                MonitorEvent event = mon.recv(ReceiveFlag.DONTWAIT);
                if (event.event() == forbidden) {
                    fail("unexpected monitor event=" + forbidden);
                }
            } catch (RuntimeException ex) {
                TestSupport.sleepMs(TestSupport.POLL_INTERVAL_MS);
            }
        }
    }
}
