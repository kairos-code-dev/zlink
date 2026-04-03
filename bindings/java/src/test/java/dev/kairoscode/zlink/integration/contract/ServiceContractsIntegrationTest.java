package dev.kairoscode.zlink.integration.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.TestSupport;
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

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
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
             var monitor = socket.monitorOpen(MonitorEventType.ALL.getValue())) {
            assertTrue(monitor.snapshot().sndPendingMsgs() >= 0L);
        }

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = new Spot(node)) {
            spot.setSubscription("svc-topic");
            assertEquals(0, node.statusSnapshot().connectedPeerCount());
            assertTrue(node.subjectsSnapshot().stream()
                .anyMatch(entry -> "svc-topic".equals(entry.subject())));
        }
    }

    @Test
    void spotNodeWrappedHandleExposesCanonicalServiceHandle() {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode serverNode = new SpotNode(ctx);
             SpotNode clientNode = new SpotNode(ctx);
             Spot publisher = new Spot(serverNode);
             Spot subscriber = new Spot(clientNode)) {
            serverNode.bind(endpoint);
            clientNode.connectPeer(endpoint);
            subscriber.setSubscription("perf-topic");
            awaitCondition(() -> clientNode.statusSnapshot().connectedPeerCount() > 0,
                "spot peer connection");
            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (var payload = dev.kairoscode.zlink.Message.copyOfUtf8("perf-body")) {
                    assertEquals(SendResult.SENT,
                        publisher.tryPublish("perf-topic", payload));
                }
                var delivery = subscriber.trySubscribe();
                if (delivery.isPresent()) {
                    try (var topicMessage = delivery.get()) {
                        assertEquals("perf-topic", topicMessage.topicId());
                        assertArrayEquals("perf-body".getBytes(),
                            topicMessage.singlePartOrThrow().toByteArray());
                        return;
                    }
                }
                Thread.onSpinWait();
            }
            assertFalse(true, "spot publish/subscribe delivery timed out");
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
