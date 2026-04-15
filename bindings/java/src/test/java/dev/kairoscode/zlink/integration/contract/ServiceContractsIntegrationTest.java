package dev.kairoscode.zlink.integration.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.service.registry.ServiceEvent;
import dev.kairoscode.zlink.service.registry.ServiceEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.RegistryQueryClient;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.time.Duration;
import java.time.Instant;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ServiceContractsIntegrationTest {
    @Test
    void discoveryRegistryAndSpotNodeExposeCanonicalSnapshots() {
        TestSupport.assumeNative();

        String registryPub = TestSupport.tcpEndpoint();
        String registryRouter = TestSupport.tcpEndpoint();

        try (Context ctx = new Context();
            Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SOCKET,
               "svc-alpha");
             SpotNode node = new SpotNode(ctx);
             RegistryQueryClient queryClient = new RegistryQueryClient(ctx)) {
            registry.bind(registryPub, registryRouter);
            queryClient.connect(registryRouter);
            discovery.connectRegistry(registryRouter);
            discovery.setValue(7L);
            discovery.setMetadata("meta".getBytes());
            String cert = Path.of("tests/certs/server.crt").toAbsolutePath().toString();
            String key = Path.of("tests/certs/server.key").toAbsolutePath().toString();
            String ca = Path.of("tests/certs/ca.crt").toAbsolutePath().toString();
            registry.setTlsServer(cert, key, true);
            registry.setTlsClient(ca, "localhost", true);
            discovery.setTlsClient(ca, "localhost", true);

            assertEquals(7L, discovery.getValue());
            assertArrayEquals("meta".getBytes(), discovery.getMetadata());
            assertTrue(discovery.memberPeers().isEmpty());

            assertEquals(0, registry.statusSnapshot().topologyEntryCount());
            assertTrue(registry.serviceSummarySnapshot().isEmpty());
            assertTrue(registry.topologySnapshot().isEmpty());
            assertTrue(registry.memberPeers(ServiceType.SOCKET,
              "svc-alpha").isEmpty());
            assertTrue(queryClient.snapshot().isEmpty());

            assertEquals(0, node.statusSnapshot().configuredPeerCount());
            assertTrue(node.peersSnapshot().isEmpty());
            assertTrue(node.subjectsSnapshot().isEmpty());
        }
    }

    @Test
    void monitorSnapshotExposesCanonicalMonitorState() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket socket = new PairSocket(ctx);
             var monitor = socket.monitorOpen()) {
            assertTrue(monitor.snapshot().sndPendingMsgs() >= 0L);
        }

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.SOCKET,
               "svc-monitor");
             var monitor = discovery.monitorOpen()) {
            assertDoesNotThrow(monitor::snapshot);
        }

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot()) {
            spot.setSubscription("svc-topic");
            assertEquals(0, node.statusSnapshot().connectedPeerCount());
            assertTrue(node.subjectsSnapshot().stream()
                .anyMatch(entry -> "svc-topic".equals(entry.subject())));
        }
    }

    @Test
    void serviceMonitorOnEventRunsOnManagedJavaThread() throws Exception {
        TestSupport.assumeNative();

        String registryPub = TestSupport.tcpEndpoint();
        String registryRouter = TestSupport.tcpEndpoint();
        String serviceEndpoint = TestSupport.tcpEndpoint();
        String callbackEndpoint = TestSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<ServiceEvent> eventRef = new AtomicReference<>();
        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        AtomicReference<String> rejectionMessage = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();
        Thread testThread = Thread.currentThread();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT,
               "svc-monitor-callback");
             SpotNode node = new SpotNode(ctx);
             PairSocket callbackLeft = new PairSocket(ctx);
             PairSocket callbackRight = new PairSocket(ctx);
             var callbackLeftMon = callbackLeft.monitorOpen(
               MonitorEventType.CONNECTION_READY);
             var callbackRightMon = callbackRight.monitorOpen(
               MonitorEventType.CONNECTION_READY);
            ServiceMonitor monitor = discovery.monitorOpen()) {
            monitor.onEvent(event -> {
                try {
                    callbackThread.set(Thread.currentThread());
                    eventRef.set(event);
                    try (Message reply = Message.copyOfUtf8("callback-send")) {
                        try {
                            callbackLeft.send(reply);
                            throw new IllegalStateException(
                              "blocking send in service monitor callback must be rejected");
                        } catch (IllegalStateException ex) {
                            rejectionMessage.set(ex.getMessage());
                        }
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    delivered.countDown();
                }
            });

            callbackLeft.bind(callbackEndpoint);
            callbackRight.connect(callbackEndpoint);
            callbackLeftMon.recv();
            callbackRightMon.recv();
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            node.attachDiscovery(discovery);
            node.bind(serviceEndpoint);

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS), "service monitor callback timed out");
            ServiceEvent event = eventRef.get();
            assertNotNull(event);
            assertEquals(ServiceEventType.DISCOVERY_SERVICE_UP,
                event.eventType());
            assertEquals("svc-monitor-callback", event.serviceName());
            assertNull(callbackError.get(), "callback raised: "
                + callbackError.get());
            Thread observedThread = callbackThread.get();
            assertNotNull(observedThread);
            assertTrue(observedThread != testThread);
            assertTrue(observedThread.getName().startsWith(
                "zlink-service-monitor-callback"));
            assertNotNull(rejectionMessage.get());
            assertTrue(rejectionMessage.get().contains("blocking send"));
            assertTrue(rejectionMessage.get().contains("callback context"));
        }
    }

    @Test
    void spotNodeWrappedHandleExposesCanonicalServiceHandle() {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode serverNode = new SpotNode(ctx);
             SpotNode clientNode = new SpotNode(ctx);
             Spot publisher = serverNode.createSpot();
             Spot subscriber = clientNode.createSpot()) {
            serverNode.bind(endpoint);
            clientNode.connectPeer(endpoint);
            subscriber.setSubscription("perf-topic");
            awaitCondition(() -> clientNode.statusSnapshot().connectedPeerCount() > 0,
                "spot peer connection");
            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (var payload = dev.kairoscode.zlink.Message.copyOfUtf8("perf-body")) {
                    publisher.publish("spot-perf-service", "perf-topic", payload,
                        SendFlags.DONT_WAIT);
                }
                try (var topicMessage = subscriber.subscribe(RecvFlags.DONT_WAIT)) {
                    assertEquals("spot-perf-service",
                        topicMessage.serviceName().orElseThrow());
                    assertEquals("perf-topic", topicMessage.topic());
                    assertArrayEquals("perf-body".getBytes(),
                        topicMessage.singlePartOrThrow().toByteArray());
                    return;
                } catch (RecvException ex) {
                    if (ex.getResult() != RecvResult.NO_DATA) {
                        throw ex;
                    }
                }
                Thread.onSpinWait();
            }
            assertFalse(true, "spot publish/subscribe delivery timed out");
        }
    }

    @Test
    void spotDispatchEventRunsOnManagedJavaThread() throws Exception {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        AtomicReference<String> serviceNameRef = new AtomicReference<>();
        AtomicReference<String> topicRef = new AtomicReference<>();
        AtomicReference<byte[]> payloadRef = new AtomicReference<>();
        AtomicReference<String> rejectionMessage = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();
        Thread testThread = Thread.currentThread();

        try (Context ctx = new Context();
             SpotNode serverNode = new SpotNode(ctx);
             SpotNode clientNode = new SpotNode(ctx);
             Spot publisher = serverNode.createSpot();
             Spot subscriber = clientNode.createSpot()) {
            serverNode.bind(endpoint);
            clientNode.connectPeer(endpoint);
            subscriber.setSubscription("spot-callback-topic");
            awaitCondition(() -> clientNode.statusSnapshot().connectedPeerCount() > 0,
                "spot peer connection");

            subscriber.onDispatchEvent(event -> {
                if (event != SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                try {
                    callbackThread.set(Thread.currentThread());
                    try (var received = subscriber.subscribe(RecvFlags.DONT_WAIT)) {
                        serviceNameRef.set(received.serviceName().orElseThrow());
                        topicRef.set(received.topic());
                        payloadRef.set(received.singlePartOrThrow().toByteArray());
                    }
                    try (Message reply = Message.copyOfUtf8("callback-send")) {
                        try {
                            subscriber.publish("spot-callback-service",
                                topicRef.get(), reply, SendFlags.DONT_WAIT);
                            throw new IllegalStateException(
                                "blocking publish in Spot callback must be rejected");
                        } catch (IllegalStateException ex) {
                            rejectionMessage.set(ex.getMessage());
                        }
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    delivered.countDown();
                }
            });

            try (Message payload = Message.copyOfUtf8("spot-body")) {
                publisher.publish("spot-callback-service",
                    "spot-callback-topic", payload, SendFlags.DONT_WAIT);
            }

            assertTrue(delivered.await(TestSupport.DEFAULT_TIMEOUT_MS,
                TimeUnit.MILLISECONDS), "spot callback timed out");
            assertNull(callbackError.get(), "callback raised: "
                + callbackError.get());
            assertEquals("spot-callback-service", serviceNameRef.get());
            assertEquals("spot-callback-topic", topicRef.get());
            assertArrayEquals("spot-body".getBytes(), payloadRef.get());
            Thread observedThread = callbackThread.get();
            assertNotNull(observedThread);
            assertTrue(observedThread != testThread);
            assertTrue(observedThread.getName().startsWith(
                "zlink-spot-dispatch-callback"));
            assertNotNull(rejectionMessage.get());
            assertTrue(rejectionMessage.get().contains("blocking publish"));
            assertTrue(rejectionMessage.get().contains("callback context"));
        }
    }

    private static boolean await(CountDownLatch latch) {
        try {
            return latch.await(TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("await interrupted", ex);
        }
    }

    private static void awaitCondition(Check check, String label) {
        Instant deadline = Instant.now().plusMillis(TestSupport.DEFAULT_TIMEOUT_MS);
        while (Instant.now().isBefore(deadline)) {
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
