package systems.zlink;

import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.errors.ZlinkBindException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotDispatchSubjectKind;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.messaging.TopicMessage;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class CallbackModeContractTest {
    @Test
    public void onReceiveDeliversReceivedAndClosesFramesAfterReturn() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket left = ctx.createPairSocket();
             PairSocket right = ctx.createPairSocket()) {
            String endpoint = TestSupport.inprocEndpoint("callback-recv");
            left.bind(endpoint);
            right.connect(endpoint);

            try (Message outbound = Message.from("callback-body")) {
                right.send().message(outbound).submit();
            }

            try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {


                left.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                assertArrayEquals("callback-body".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
                assertTrue(received.isSinglePart());
            }
            try (Received probe = new Received()) {
                assertFalse(left.recv(probe, systems.zlink.contracts.sockets.RecvFlags.DONT_WAIT));
            }
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

        try (Context ctx = Zlink.createContext();
             Registry registry = ctx.createRegistry();
             Discovery discovery = ctx.createDiscovery(AutoConnectType.SPOT_MESH, channelName);
             SpotNode publisherNode = ctx.createSpotNode();
             SpotNode subscriberNode = ctx.createSpotNode();
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            String registryPub = TestSupport.tcpEndpoint();
            String registryRouter = TestSupport.tcpEndpoint();
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            publisherNode.attachDiscovery(discovery);
            bindPubWithRetry(publisherNode);
            String endpoint = publisherNode.status().localEndpoint();
            subscriberNode.connectPeer(endpoint);
            subscriber.setSubscription("alpha");
            awaitCondition(() -> subscriberNode.status()
                .connectedPeerCount() > 0, "spot peer connection");

            subscriber.setDispatchHandler(info -> {
                if (info.event() != SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                callbackThread.set(Thread.currentThread());
                eventRef.set(info.event());
                assertEquals(SpotDispatchSubjectKind.SPOT, info.subjectKind());
                delivered.countDown();
            });

            try (Message part = Message.from("payload")) {
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

        try (Context ctx = Zlink.createContext();
             Registry registry = ctx.createRegistry();
             Discovery discovery = ctx.createDiscovery(AutoConnectType.SPOT_MESH, "dispatch-drain");
             SpotNode publisherNode = ctx.createSpotNode();
             SpotNode subscriberNode = ctx.createSpotNode();
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            String registryPub = TestSupport.tcpEndpoint();
            String registryRouter = TestSupport.tcpEndpoint();
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            publisherNode.attachDiscovery(discovery);
            bindPubWithRetry(publisherNode);
            subscriberNode.connectPeer(publisherNode.status()
                .localEndpoint());
            subscriber.setSubscription("close-race");
            awaitCondition(() -> subscriberNode.status()
                .connectedPeerCount() > 0, "spot peer connection");

            subscriber.setDispatchHandler(info -> {
                if (info.event() == SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    delivered.countDown();
                }
            });

            for (int i = 0; i < 64; i++) {
                try (Message part = Message.from("payload-" + i)) {
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

        try (Context ctx = Zlink.createContext();
             SpotNode publisherNode = ctx.createSpotNode();
             SpotNode subscriberNode = ctx.createSpotNode();
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            bindPubWithRetry(publisherNode);
            subscriberNode.connectPeer(publisherNode.status()
                .localEndpoint());
            subscriber.setSubscription("drain");
            awaitCondition(() -> subscriberNode.status()
                .connectedPeerCount() > 0, "spot peer connection");
            subscriber.setDispatchHandler(info -> {
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

            long deadline = System.nanoTime()
              + TimeUnit.MILLISECONDS.toNanos(TestSupport.DEFAULT_TIMEOUT_MS);
            while (drained.getCount() > 0 && System.nanoTime() < deadline) {
                try (Message part = Message.from("dispatch-drain")) {
                    publisher.publish("drain").message(part).submit();
                }
                drained.await(10, TimeUnit.MILLISECONDS);
            }

            assertTrue(drained.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS));
            assertEquals("dispatch-drain", payloadRef.get());
        }
    }

    @Test
    public void spotRequestCallbackCompletesOnNativeTimeout()
      throws Exception {
        TestSupport.assumeNative();

        CountDownLatch completed = new CountDownLatch(1);
        AtomicReference<RequestResult> resultRef = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();
        RoutingId serverNodeRid = RoutingId.from(
            "timeout-server-node".getBytes(StandardCharsets.UTF_8));
        RoutingId serverSpotRid = RoutingId.from(
            "timeout-server-spot".getBytes(StandardCharsets.UTF_8));

        try (Context ctx = Zlink.createContext();
             SpotNode serverNode = ctx.createSpotNode();
             SpotNode clientNode = ctx.createSpotNode();
             Spot replier = serverNode.createSpot();
             Spot requester = clientNode.createSpot()) {
            serverNode.setRoutingId(serverNodeRid);
            replier.setRoutingId(serverSpotRid);
            replier.setDispatchHandler(info -> {
            });
            bindRouterWithRetry(serverNode);
            bindRouterWithRetry(clientNode);
            bindPubWithRetry(serverNode);
            clientNode.connectPeer(serverNode.status()
                .localEndpoint());
            awaitCondition(() -> clientNode.status()
                .connectedPeerCount() > 0, "spot request peer connection");

            long submitDeadline = System.nanoTime()
              + TimeUnit.MILLISECONDS.toNanos(TestSupport.DEFAULT_TIMEOUT_MS);
            boolean submitted = false;
            while (!submitted && System.nanoTime() < submitDeadline) {
                try (Message request = Message.from("timeout-request")) {
                    requester.requestToSpot(serverNodeRid, serverSpotRid)
                        .message(request)
                        .timeout(Duration.ofMillis(50))
                        .flags(SendFlags.DONT_WAIT)
                        .submit((result, parts) -> {
                            try {
                                resultRef.set(result);
                            } catch (Throwable error) {
                                callbackError.set(error);
                            } finally {
                                Message.closeAll(parts);
                                completed.countDown();
                            }
                        });
                    submitted = true;
                } catch (ZlinkSubmitException ex) {
                    if (!isTransientSubmit(ex.getResult())) {
                        throw ex;
                    }
                    TimeUnit.MILLISECONDS.sleep(10);
                }
            }
            assertTrue(submitted, "spot request submit did not become ready");

            assertTrue(completed.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS), "spot request callback timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
            assertEquals(RequestResult.TIMED_OUT, resultRef.get());
        }
    }

    private static boolean isTransientSubmit(SubmitResult result) {
        return result == SubmitResult.BACKPRESSURED
          || result == SubmitResult.NOT_CONNECTED
          || result == SubmitResult.NOT_FOUND;
    }

    private static void bindPubWithRetry(SpotNode node) {
        bindWithRetry(endpoint -> node.setPubBind(endpoint));
    }

    private static void bindRouterWithRetry(SpotNode node) {
        bindWithRetry(endpoint -> node.setRouterBind(endpoint));
    }

    private static void bindWithRetry(BindAction action) {
        ZlinkBindException last = null;
        for (int i = 0; i < 16; i++) {
            try {
                action.bind(TestSupport.tcpEndpoint());
                return;
            } catch (ZlinkBindException ex) {
                last = ex;
            }
        }
        throw last;
    }

    @FunctionalInterface
    private interface BindAction {
        void bind(String endpoint);
    }

    @Test
    public void onSendReadyReplacesHandlerWithoutError() {
        TestSupport.assumeNative();

        AtomicInteger installs = new AtomicInteger();

        try (Context ctx = Zlink.createContext();
             PairSocket left = ctx.createPairSocket();
             PairSocket right = ctx.createPairSocket()) {
            String endpoint = TestSupport.inprocEndpoint("callback-send-ready");
            left.bind(endpoint);
            right.connect(endpoint);

            left.setSendReadyHandler(() -> installs.addAndGet(100));
            left.setSendReadyHandler(installs::incrementAndGet);
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
