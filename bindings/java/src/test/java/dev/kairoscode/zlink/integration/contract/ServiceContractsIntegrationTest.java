package dev.kairoscode.zlink.integration.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.RegistryQueryClient;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

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
             Socket socket = new Socket(ctx, SocketType.PAIR);
             var monitor = socket.monitorOpen(MonitorEventType.ALL.getValue())) {
            assertTrue(monitor.snapshot().readyCount() >= 0);
        }

        try (Context ctx = new Context();
             Spot spot = new Spot(ctx);
             ServiceMonitor monitor = spot.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            assertTrue(monitor.snapshot().readyCount() >= 0);
        }
    }
}
