package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.options.SocketOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
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
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx)) {
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
             XPubSocket pub = new XPubSocket(ctx);
             SubSocket sub = new SubSocket(ctx)) {
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

            SubscriptionEvent event = pub.subscriptionEvent();
            assertTrue(event.subscribed());
            assertEquals("alpha", event.filter());
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
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("callback-send-ready");
            left.bind(endpoint);
            right.connect(endpoint);

            left.onSendReady(() -> installs.addAndGet(100));
            left.onSendReady(installs::incrementAndGet);
            assertEquals(0, installs.get());
        }
    }

    private static void publish(XPubSocket subject, String topic,
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

}
