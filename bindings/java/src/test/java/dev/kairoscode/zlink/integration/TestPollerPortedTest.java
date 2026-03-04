package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestPollerPortedTest {
    @Test
    public void testPollerDetectsReadablePairSocket() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("poller");
            a.bind(endpoint);
            b.connect(endpoint);
            TestSupport.sleepMs(50);

            Poller poller = new Poller();
            poller.add(a, PollEventType.POLLIN);

            b.send("hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            assertTrue(poller.pollAny(TestSupport.DEFAULT_TIMEOUT_MS));
            List<Poller.PollEvent> events = poller.poll(10);
            assertFalse(events.isEmpty());
            assertTrue((events.get(0).revents() & PollEventType.POLLIN.getValue())
                != 0);

            byte[] payload = a.recv(32, dev.kairoscode.zlink.ReceiveFlag.NONE);
            assertEquals("hello", new String(payload, StandardCharsets.UTF_8));
        }
    }

    @Test
    public void testPollerNoEventWhenIdle() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket a = new Socket(ctx, SocketType.PAIR);
             Socket b = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("poller-idle");
            a.bind(endpoint);
            b.connect(endpoint);
            TestSupport.sleepMs(50);

            Poller poller = new Poller();
            poller.add(a, PollEventType.POLLIN);

            int ready = poller.pollCount(50);
            assertEquals(0, ready);
        }
    }
}
