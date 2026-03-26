package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.SubscriptionEntry;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SocketSubscriptionContractTest {
    @Test
    public void subscriptionsSnapshotReflectsDedicatedHelpers() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            sub.setSubscription("topic-a");
            sub.setSubscription("topic-b");
            sub.unsetSubscription("topic-b");

            List<SubscriptionEntry> entries = sub.subscriptions();
            assertEquals(1, entries.size());
            assertArrayEquals("topic-a".getBytes(StandardCharsets.UTF_8),
                entries.get(0).filter());
        }
    }

    @Test
    public void publishUsesCanonicalTopicPath() throws Exception {
        TestSupport.assumeNative();

        int readyEvents = MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue()
            | MonitorEventType.PUB_DELIVERY_READY_CHANGED.getValue();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> topic = new AtomicReference<>();
        AtomicReference<byte[]> payload = new AtomicReference<>();
        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub = new Socket(ctx, SocketType.SUB);
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
            subMonitor.recv();
            pubMonitor.recv();

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

        int readyEvents = MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue()
            | MonitorEventType.PUB_DELIVERY_READY_CHANGED.getValue();
        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub = new Socket(ctx, SocketType.SUB);
             var pubMonitor = pub.monitorOpen(readyEvents);
             var subMonitor = sub.monitorOpen(readyEvents)) {
            String endpoint = TestSupport.inprocEndpoint("subscribe-contract");
            pub.bind(endpoint);
            sub.setSubscription("topic-b");
            sub.connect(endpoint);
            subMonitor.recv();
            pubMonitor.recv();

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
}
