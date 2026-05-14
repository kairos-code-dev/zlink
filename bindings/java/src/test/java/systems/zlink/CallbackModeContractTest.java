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
                right.send().message(outbound).submit();
            }

            try (systems.zlink.Received received = new systems.zlink.Received()) {


                left.recv(received, systems.zlink.RecvFlags.NONE);
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

        String channelName = "spot-callback-service";
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<SpotDispatchEvent> eventRef = new AtomicReference<>();
        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        Thread testThread = Thread.currentThread();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, AutoConnectType.SPOT_MESH,
                 channelName);
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
                publisher.publish("alpha").message(part).submit();
            }

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertNotNull(callbackThread.get());
            assertTrue(callbackThread.get() != testThread);
            assertEquals(SpotDispatchEvent.SUBSCRIBE_READABLE,
                eventRef.get());
        }
    }

    @Test
    public void onDispatchEventCloseDoesNotCallDestroyedUpcallStub()
      throws Exception {
        TestSupport.assumeNative();

        CountDownLatch delivered = new CountDownLatch(1);

        try (Context ctx = new Context();
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            publisherNode.bind(TestSupport.tcpEndpoint());
            subscriberNode.connectPeer(publisherNode.statusSnapshot()
                .localEndpoint());
            subscriber.setSubscription("close-race");
            awaitCondition(() -> subscriberNode.statusSnapshot()
                .connectedPeerCount() > 0, "spot peer connection");

            subscriber.onDispatchEvent(info -> {
                if (info.event() == SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    delivered.countDown();
                }
            });

            for (int i = 0; i < 64; i++) {
                try (Message part = Message.copyOfUtf8("payload-" + i)) {
                    publisher.publish("close-race").message(part).submit();
                }
            }

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            subscriber.close();
            Thread.sleep(100);
        }
    }

    @Test
    public void onDispatchEventAllowsSubscriptionDrainInsideCallback()
      throws Exception {
        TestSupport.assumeNative();

        CountDownLatch drained = new CountDownLatch(1);
        AtomicReference<String> payloadRef = new AtomicReference<>();

        try (Context ctx = new Context();
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            publisherNode.bind(TestSupport.tcpEndpoint());
            subscriberNode.connectPeer(publisherNode.statusSnapshot()
                .localEndpoint());
            subscriber.setSubscription("drain");
            awaitCondition(() -> subscriberNode.statusSnapshot()
                .connectedPeerCount() > 0, "spot peer connection");

            subscriber.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                try (TopicMessage message = new TopicMessage()) {
                    if (subscriber.subscribe(message, RecvFlags.DONT_WAIT)) {
                        payloadRef.set(message.firstPart().toUtf8String());
                        drained.countDown();
                    }
                }
            });

            try (Message part = Message.copyOfUtf8("dispatch-drain")) {
                publisher.publish("drain").message(part).submit();
            }

            assertTrue(drained.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertEquals("dispatch-drain", payloadRef.get());
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
