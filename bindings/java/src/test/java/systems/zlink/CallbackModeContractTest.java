package systems.zlink;

import systems.zlink.SpotDispatchEvent;
import systems.zlink.SpotDispatchSubjectKind;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class CallbackModeContractTest {
    @Test
    public void onReceiveDeliversReceivedAndClosesFramesAfterReturn() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("callback-recv");
            left.bind(endpoint);
            right.connect(endpoint);

            try (Message outbound = Message.copyOfUtf8("callback-body")) {
                right.send(outbound);
            }

            try (var received = left.recv()) {
                assertArrayEquals("callback-body".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
                assertTrue(received.isSinglePart());
            }
            assertTrue(left.recvNoWait().isEmpty());
        }
    }

    @Test
    public void onDispatchEventDeliversSpotSubscriptionAndPayload()
      throws Exception {
        TestSupport.assumeNative();

        String serviceName = "spot-callback-service";
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<SpotDispatchEvent> eventRef = new AtomicReference<>();
        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        Thread testThread = Thread.currentThread();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, AutoConnectType.SPOT_MESH,
                 serviceName);
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            String registryPub = TestSupport.tcpEndpoint();
            String registryRouter = TestSupport.tcpEndpoint();
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            publisherNode.attachDiscovery(discovery);
            publisherNode.bind(TestSupport.tcpEndpoint());
            String endpoint = publisherNode.statusSnapshot().localEndpoint();
            subscriberNode.connectPeer(endpoint);
            subscriber.setSubscription("alpha");
            awaitCondition(() -> subscriberNode.statusSnapshot()
                .connectedPeerCount() > 0, "spot peer connection");

            subscriber.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                callbackThread.set(Thread.currentThread());
                eventRef.set(info.event());
                assertEquals(SpotDispatchSubjectKind.SPOT, info.subjectKind());
                delivered.countDown();
            });

            try (Message part = Message.copyOfUtf8("payload")) {
                publisher.publish(serviceName, "alpha").message(part).submit();
            }

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertNotNull(callbackThread.get());
            assertTrue(callbackThread.get() != testThread);
            assertTrue(callbackThread.get().getName().startsWith(
                "zlink-spot-dispatch-callback"));
            assertEquals(SpotDispatchEvent.SUBSCRIBE_READABLE,
                eventRef.get());
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

    private static void awaitCondition(Check check, String label) {
        long deadlineNanos = System.nanoTime()
          + TimeUnit.MILLISECONDS.toNanos(TestSupport.DEFAULT_TIMEOUT_MS);
        while (System.nanoTime() < deadlineNanos) {
            if (check.ready()) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new IllegalStateException(label + " timed out");
    }

    @FunctionalInterface
    private interface Check {
        boolean ready();
    }
}
