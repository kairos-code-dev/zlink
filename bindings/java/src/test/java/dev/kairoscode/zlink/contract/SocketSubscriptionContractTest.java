package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubscriptionEvent;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.XPubSocket;
import dev.kairoscode.zlink.XSubSocket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SocketSubscriptionContractTest {
    @Test
    public void subscriptionHelpersRemainCanonicalWithoutSnapshotSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SubSocket sub = new SubSocket(ctx)) {
            sub.setSubscription("topic-a");
            sub.setSubscription("topic-b");
            sub.unsetSubscription("topic-b");
        }
    }

    @Test
    public void publishUsesCanonicalTopicPath() throws Exception {
        TestSupport.assumeNative();

        int readyEvents = MonitorEventType.CONNECTION_READY.getValue();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> topic = new AtomicReference<>();
        AtomicReference<byte[]> payload = new AtomicReference<>();
        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(readyEvents);
             var subMonitor = sub.monitorOpen(readyEvents)) {
            String endpoint = TestSupport.inprocEndpoint("publish-contract");
            sub.onSubscribe((routingId, receivedTopic, received) -> {
                try (received) {
                    topic.set(receivedTopic);
                    payload.set(received.singlePartOrThrow().toByteArray());
                    delivered.countDown();
                }
            });
            pub.bind(endpoint);
            sub.setSubscription("topic-a");
            sub.connect(endpoint);
            TestSupport.awaitMonitorEvent(subMonitor,
                MonitorEventType.CONNECTION_READY);
            TestSupport.awaitMonitorEvent(pubMonitor,
                MonitorEventType.CONNECTION_READY);

            try (Message part = Message.copyOfUtf8("payload")) {
                pub.publish("topic-a", part);
            }

            assertTrue(delivered.await(5, TimeUnit.SECONDS));
            assertEquals("topic-a", topic.get());
            assertArrayEquals("payload".getBytes(StandardCharsets.UTF_8),
                payload.get());
        }
    }

    @Test
    public void subscribePullsTopicAwareMessage() {
        TestSupport.assumeNative();

        int readyEvents = MonitorEventType.CONNECTION_READY.getValue();
        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(readyEvents);
             var subMonitor = sub.monitorOpen(readyEvents)) {
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
                assertEquals("topic-b", received.topicId());
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
            assertEquals("manual-topic", event.filter());
        }
    }
}
