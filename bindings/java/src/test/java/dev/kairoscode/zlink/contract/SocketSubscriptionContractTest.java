package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.SubscriptionEvent;
import dev.kairoscode.zlink.SubscriptionEntry;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.XSubSocket;
import dev.kairoscode.zlink.XPubSocket;
import java.lang.reflect.RecordComponent;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.List;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SocketSubscriptionContractTest {
    @Test
    public void subscriptionHelpersRemainCanonicalWithoutSnapshotSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SubSocket sub = new SubSocket(ctx)) {
            sub.setSubscription("topic-a");
            sub.setSubscription("topic-b");
            assertEquals(2, sub.options().topicsCount());
            List<String> filters = List.of(
                sub.subscriptionAt(0).map(SubscriptionEntry::filter)
                    .orElseThrow(),
                sub.subscriptionAt(1).map(SubscriptionEntry::filter)
                    .orElseThrow());
            assertTrue(filters.contains("topic-a"));
            assertTrue(filters.contains("topic-b"));
            assertTrue(sub.subscriptionAt(2).isEmpty());
            sub.unsetSubscription("topic-b");
        }
    }

    @Test
    public void publishUsesCanonicalTopicPath() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             XPubSocket pub = new XPubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            String endpoint = TestSupport.inprocEndpoint("publish-contract");
            pub.bind(endpoint);
            sub.setSubscription("topic-a");
            sub.connect(endpoint);
            TestSupport.awaitMonitorEvent(subMonitor,
                MonitorEventType.CONNECTION_READY);
            TestSupport.awaitMonitorEvent(pubMonitor,
                MonitorEventType.CONNECTION_READY);

            SubscriptionEvent event = pub.receiveSubscriptionEvent();
            assertTrue(event.subscribed());
            assertEquals("topic-a", event.topic());

            try (Message part = Message.copyOfUtf8("payload")) {
                pub.publish("topic-a", part);
            }

            try (TopicMessage received = sub.subscribe()) {
                assertEquals("topic-a", received.topic());
                assertArrayEquals("payload".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void subscribePullsTopicAwareMessage() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            String endpoint = TestSupport.inprocEndpoint("subscribe-contract");
            pub.bind(endpoint);
            sub.setSubscription("topic-b");
            sub.connect(endpoint);
            TestSupport.awaitMonitorEvent(subMonitor,
                MonitorEventType.CONNECTION_READY);
            TestSupport.awaitMonitorEvent(pubMonitor,
                MonitorEventType.CONNECTION_READY);

            try (Message part = Message.copyOfUtf8("payload-b")) {
                pub.publish("topic-b", part);
            }

            try (TopicMessage received = sub.subscribe()) {
                assertEquals("topic-b", received.topic());
                assertArrayEquals("payload-b".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void xpubSubscriptionEventUsesDedicatedPubOptionSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             XPubSocket pub = new XPubSocket(ctx);
             XSubSocket sub = new XSubSocket(ctx)) {
            pub.options().manual(true);
            String endpoint = TestSupport.inprocEndpoint("xpub-manual-contract");
            pub.bind(endpoint);
            sub.setSubscription("manual-topic");
            sub.connect(endpoint);

            SubscriptionEvent event = pub.receiveSubscriptionEvent();
            assertTrue(event.subscribed());
            assertEquals("manual-topic", event.topic());
        }
    }

    @Test
    public void subscriptionEventRecordShapeMatchesSpec() {
        RecordComponent[] components = SubscriptionEvent.class.getRecordComponents();
        assertNotNull(components);
        assertEquals(4, components.length);
        assertEquals("routingId", components[0].getName());
        assertEquals("serviceName", components[1].getName());
        assertEquals("topic", components[2].getName());
        assertEquals("subscribed", components[3].getName());
    }
}
