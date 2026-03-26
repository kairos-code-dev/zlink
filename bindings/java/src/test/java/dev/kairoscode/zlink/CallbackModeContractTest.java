package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.options.SocketOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class CallbackModeContractTest {
    @Test
    public void onReceiveDeliversReceivedAndClosesFramesAfterReturn() throws Exception {
        TestSupport.assumeNative();

        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<Message> deliveredPart = new AtomicReference<>();
        AtomicReference<byte[]> payload = new AtomicReference<>();

        try (Context ctx = new Context();
             Socket left = new Socket(ctx, SocketType.PAIR);
             Socket right = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("callback-recv");
            left.bind(endpoint);
            right.connect(endpoint);

            left.onReceive(received -> {
                deliveredPart.set(received.singlePartOrThrow());
                payload.set(received.singlePartOrThrow().toByteArray());
                delivered.countDown();
            });

            try (Message outbound = Message.copyOfUtf8("callback-body")) {
                right.send(outbound);
            }

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertArrayEquals("callback-body".getBytes(StandardCharsets.UTF_8),
                payload.get());
            assertNotNull(deliveredPart.get());
            assertFalse(deliveredPart.get().valid());
            ZlinkException ex = assertThrows(ZlinkException.class,
                () -> left.recv(ReceiveFlag.DONTWAIT));
            assertEquals(16, ex.errno());
        }
    }

    @Test
    public void onSubscribeDeliversTopicAndPayload() throws Exception {
        TestSupport.assumeNative();

        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> topic = new AtomicReference<>();
        AtomicReference<byte[]> payload = new AtomicReference<>();

        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.tcpEndpoint();
            pub.bind(endpoint);
            pub.setOption(SocketOptions.RCVTIMEO, TestSupport.DEFAULT_TIMEOUT_MS);
            sub.setSubscription("alpha");
            sub.connect(endpoint);
            sub.onSubscribe((routingId, deliveredTopic, received) -> {
                topic.set(deliveredTopic);
                payload.set(received.singlePartOrThrow().toByteArray());
                delivered.countDown();
            });

            SubscriptionEvent event = waitForSubscriptionEvent(pub);
            assertTrue(event.subscribed);
            assertArrayEquals("alpha".getBytes(StandardCharsets.UTF_8),
                event.topic);
            publish(pub, "alpha", List.of(Message.copyOfUtf8("payload")));

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertEquals("alpha", topic.get());
            assertArrayEquals("payload".getBytes(StandardCharsets.UTF_8),
                payload.get());
        }
    }

    @Test
    public void onSendReadyReplacesHandlerWithoutError() {
        TestSupport.assumeNative();

        AtomicInteger installs = new AtomicInteger();

        try (Context ctx = new Context();
             Socket left = new Socket(ctx, SocketType.PAIR);
             Socket right = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("callback-send-ready");
            left.bind(endpoint);
            right.connect(endpoint);

            left.onSendReady(() -> installs.addAndGet(100));
            left.onSendReady(installs::incrementAndGet);
            assertEquals(0, installs.get());
        }
    }

    private static void publish(Socket subject, String topic,
                                List<Message> parts) {
        try (Arena arena = Arena.ofConfined()) {
            long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
            MemorySegment nativeParts = arena.allocate(msgSize * parts.size(),
                NativeLayouts.MSG_LAYOUT.byteAlignment());
            Object[] anchors = new Object[parts.size()];
            boolean success = false;
            try {
                for (int i = 0; i < parts.size(); i++) {
                    MemorySegment nativeMsg = nativeParts.asSlice((long) i * msgSize,
                        msgSize);
                    anchors[i] = parts.get(i).transferTo(nativeMsg);
                }
                MemorySegment topicSeg = arena.allocateFrom(topic,
                    StandardCharsets.UTF_8);
                int rc = Native.publish(subject.handle(), topicSeg, nativeParts,
                    parts.size(), SendFlag.NONE.getValue());
                if (rc != 0)
                    throw ZlinkException.fromLastError("zlink_publish");
                success = true;
            } finally {
                if (!success) {
                    restore(parts, nativeParts, anchors);
                }
            }
        } finally {
            Message.closeAll(parts);
        }
    }

    private static void restore(List<Message> parts, MemorySegment nativeParts,
                                Object[] anchors) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        for (int i = 0; i < parts.size(); i++) {
            MemorySegment nativeMsg = nativeParts.asSlice((long) i * msgSize,
                msgSize);
            try {
                parts.get(i).restoreFromNative(nativeMsg, i + 1 < parts.size(),
                    anchors[i]);
            } catch (RuntimeException ignored) {
            }
        }
    }

    private static SubscriptionEvent waitForSubscriptionEvent(Socket socket) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment subscribedOut = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment topicOut = arena.allocate(256);
            MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
            topicLenOut.set(ValueLayout.JAVA_LONG, 0, 256);
            int rc = Native.subscriptionEvent(socket.handle(), MemorySegment.NULL,
                subscribedOut, topicOut, topicLenOut, 0);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_subscription_event");
            int topicLen = (int) topicLenOut.get(ValueLayout.JAVA_LONG, 0);
            byte[] topic = new byte[topicLen];
            if (topicLen > 0) {
                MemorySegment.copy(topicOut, 0, MemorySegment.ofArray(topic), 0,
                    topicLen);
            }
            return new SubscriptionEvent(
                subscribedOut.get(ValueLayout.JAVA_INT, 0) != 0, topic);
        }
    }

    private record SubscriptionEvent(boolean subscribed, byte[] topic) {}
}
