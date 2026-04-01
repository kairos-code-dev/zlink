package dev.kairoscode.zlink.integration.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.ServiceEvent;
import dev.kairoscode.zlink.ServiceMonitor;
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

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ServiceContractsIntegrationTest {
    private static final long SPOT_FILTER_APPLIED = 1L << 13;

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

            assertEquals(7L, discovery.value());
            assertArrayEquals("meta".getBytes(), discovery.metadata());
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
            assertTrue(monitor.snapshot().readyCount() >= 0);
        }

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = new Spot(node);
             ServiceMonitor monitor = spot.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            assertTrue(monitor.tryRecv().isEmpty());
            CountDownLatch ready = new CountDownLatch(1);
            AtomicReference<ServiceEvent> eventRef = new AtomicReference<>();
            monitor.onEvent(event -> {
                if ((event.eventType() & SPOT_FILTER_APPLIED) != 0) {
                    eventRef.set(event);
                    ready.countDown();
                }
            });
            spot.setSubscription("svc-topic");
            assertTrue(await(ready), "spot filter applied");
            ServiceEvent event = eventRef.get();
            assertEquals(SPOT_FILTER_APPLIED, event.eventType() & SPOT_FILTER_APPLIED);
            assertEquals("svc-topic", event.subject());
            assertTrue(monitor.snapshot().readyCount() >= 0);
        }
    }

    @Test
    void spotNodeWrappedHandleExposesCanonicalServiceHandle() {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode serverNode = new SpotNode(ctx);
             SpotNode clientNode = new SpotNode(ctx);
             Spot publisher = serverNode.wrapHandle();
             Spot subscriber = clientNode.wrapHandle();
             ServiceMonitor monitor = subscriber.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            serverNode.bind(endpoint);
            clientNode.connectPeer(endpoint);
            CountDownLatch ready = new CountDownLatch(1);
            monitor.onEvent(event -> {
                if ((event.eventType() & SPOT_FILTER_APPLIED) != 0) {
                    ready.countDown();
                }
            });
            subscriber.setSubscription("perf-topic");
            assertTrue(await(ready), "spot filter applied");
            try (var payload = dev.kairoscode.zlink.Message.copyOfUtf8("perf-body")) {
                assertEquals(SendResult.SENT,
                    publisher.tryPublish("perf-topic", payload));
            }
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
}
