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
import dev.kairoscode.zlink.SubmitException;
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
            assertNotNull(monitor);
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

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.createSpot()) {
            subscriber.setSubscription("perf-topic");
            assertTrue(node.subjectsSnapshot().stream()
                .anyMatch(entry -> "perf-topic".equals(entry.subject())));
        }
    }

    @Test
    void spotDispatchEventRunsOnManagedJavaThread() throws Exception {
        TestSupport.assumeNative();

        AtomicReference<Thread> callbackThread = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.createSpot()) {
            subscriber.setSubscription("spot-callback-topic");

            subscriber.onDispatchEvent(event -> {
                try {
                    callbackThread.set(Thread.currentThread());
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            assertNull(callbackError.get(), "callback raised: "
                + callbackError.get());
            assertTrue(node.subjectsSnapshot().stream()
                .anyMatch(entry -> "spot-callback-topic".equals(entry.subject())));
            assertNull(callbackThread.get());
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
