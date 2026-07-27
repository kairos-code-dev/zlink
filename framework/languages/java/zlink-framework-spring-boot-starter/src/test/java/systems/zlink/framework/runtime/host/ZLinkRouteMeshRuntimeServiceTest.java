package systems.zlink.framework.runtime.host;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Flow;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.internal.binding.spot.MeshMonitorEvent;
import systems.zlink.framework.runtime.internal.binding.spot.MeshMonitorStatus;
import systems.zlink.framework.runtime.internal.binding.spot.MeshNodeMonitor;
import systems.zlink.framework.runtime.internal.binding.spot.MeshNodeState;
import systems.zlink.framework.runtime.internal.binding.spot.MeshNodeStatus;
import systems.zlink.framework.runtime.internal.binding.spot.MeshPeerEntry;
import systems.zlink.framework.runtime.internal.binding.spot.MeshPeerSource;
import systems.zlink.framework.runtime.internal.binding.spot.MeshPeerState;
import systems.zlink.framework.runtime.internal.binding.spot.PeerChannels;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkCapacityUsage;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkPlacementCapacity;
import systems.zlink.framework.locations.ZLinkPlacementObjectKind;
import systems.zlink.framework.locations.ZLinkSpotTypeCapacity;
import systems.zlink.framework.locations.ZLinkActivationConcurrency;
import systems.zlink.framework.monitoring.ZLinkMeshNodeState;
import systems.zlink.framework.monitoring.ZLinkMeshRuntimeEvent;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkMeshNodeMonitoringProjection;

final class ZLinkRouteMeshRuntimeServiceTest {
    @Test
    void snapshotMapsNativeStatusAndAdvancesItsSequence() {
        FakeNode node = new FakeNode();
        try (var runtime = runtime(node)) {
            var first = runtime.snapshot("mesh");
            var second = runtime.snapshot("mesh");

            assertEquals("mesh", first.meshName());
            assertEquals(node.status().routingId(), first.rid());
            assertEquals(ZLinkMeshNodeState.SERVING, first.state());
            assertEquals(1, first.peers().size());
            assertTrue(first.peers().getFirst().ready());
            assertEquals(1, first.channels().size());
            assertEquals(7, first.channels().getFirst().localWeight());
            assertEquals(2, first.channels().getFirst().readyMemberCount());
            assertEquals(first.sequence() + 1, second.sequence());
            assertEquals(first.peers(), List.copyOf(first.peers()));
        }
    }

    @Test
    void observePublishesInitialStateWithoutWaitingForNativeTraffic() throws Exception {
        FakeNode node = new FakeNode();
        try (var runtime = runtime(node)) {
            CountDownLatch received = new CountDownLatch(1);
            AtomicReference<ZLinkMeshRuntimeEvent> event = new AtomicReference<>();
            runtime.observe("mesh", 1).subscribe(new Flow.Subscriber<>() {
                @Override
                public void onSubscribe(Flow.Subscription subscription) {
                    subscription.request(1);
                }

                @Override
                public void onNext(ZLinkMeshRuntimeEvent item) {
                    event.set(item);
                    received.countDown();
                }

                @Override
                public void onError(Throwable throwable) {
                }

                @Override
                public void onComplete() {
                }
            });

            assertTrue(received.await(2, TimeUnit.SECONDS));
            assertEquals(
                "zlink.runtime.mesh_node.state_changed",
                event.get().identifier());
            assertEquals(ZLinkMeshNodeState.SERVING, event.get().state().orElseThrow());
        }
    }

    @Test
    void snapshotAndPlacementEventProjectCapacityAndActivationChanges() throws Exception {
        FakeNode node = new FakeNode();
        AtomicReference<ZLinkMeshNodeMonitoringProjection> placement =
            new AtomicReference<>(placement(9, 2, 1));
        try (var runtime = runtime(node, placement)) {
            var snapshot = runtime.snapshot("mesh");
            assertEquals(2, snapshot.objectCapacity().actors().active());
            assertEquals(0, snapshot.objectCapacity().actors().limit());
            assertEquals(3, snapshot.objectCapacity().spots().reserved());
            assertEquals(5, snapshot.objectCapacity().spotTypes().getFirst().usage().limit());
            assertEquals(new ZLinkActivationConcurrency(1, 8), snapshot.activationConcurrency());

            CountDownLatch changed = new CountDownLatch(1);
            CountDownLatch initialized = new CountDownLatch(1);
            AtomicReference<ZLinkMeshRuntimeEvent> event = new AtomicReference<>();
            runtime.observe("mesh", 8).subscribe(new Flow.Subscriber<>() {
                @Override
                public void onSubscribe(Flow.Subscription subscription) {
                    subscription.request(Long.MAX_VALUE);
                }

                @Override
                public void onNext(ZLinkMeshRuntimeEvent item) {
                    if (item.identifier().equals("zlink.runtime.mesh_node.state_changed")) {
                        initialized.countDown();
                    }
                    if (item.identifier().equals("zlink.runtime.object.placement_changed")) {
                        event.set(item);
                        changed.countDown();
                    }
                }

                @Override
                public void onError(Throwable throwable) {
                }

                @Override
                public void onComplete() {
                }
            });

            assertTrue(initialized.await(2, TimeUnit.SECONDS));
            placement.set(placement(10, 4, 2));

            assertTrue(changed.await(2, TimeUnit.SECONDS));
            assertEquals(10L, event.get().descriptorRevision().orElseThrow());
            assertEquals("updated", event.get().reason().orElseThrow());
            assertEquals(4, runtime.snapshot("mesh").objectCapacity().actors().active());
            assertEquals(2, runtime.snapshot("mesh").activationConcurrency().active());
        }
    }

    @Test
    void runtimeOptionsApplyLiveMessageSizeChannelAndPlacementWeights() {
        FakeNode node = new FakeNode();
        var options = new ZLinkRouteMeshRuntimeOptionsService(
            () -> Map.of("mesh", node));

        options.meshNode("mesh").maxMessageSize(4096);
        options.channel("channel").weight(10_000);
        options.mesh("mesh").setPlacementWeight(0);

        assertEquals(4096, options.meshNode("mesh").maxMessageSize());
        assertEquals(10_000, options.channel("channel").weight());
        assertEquals(0, options.mesh("mesh").placementWeight());
        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.mesh("mesh").setPlacementWeight(10_001));
    }

    private static ZLinkRouteMeshRuntimeService runtime(FakeNode node) {
        return new ZLinkRouteMeshRuntimeService(
            () -> Map.of("mesh", node),
            () -> {
                throw new ZLinkConfigurationException("not configured");
            });
    }

    private static ZLinkRouteMeshRuntimeService runtime(
        FakeNode node,
        AtomicReference<ZLinkMeshNodeMonitoringProjection> placement) {
        return new ZLinkRouteMeshRuntimeService(
            () -> Map.of("mesh", node),
            () -> {
                throw new ZLinkConfigurationException("not configured");
            },
            (meshName, rid) -> placement.get());
    }

    private static ZLinkMeshNodeMonitoringProjection placement(
        long revision,
        int actorActive,
        int activationActive) {
        return new ZLinkMeshNodeMonitoringProjection(
            revision,
            ZLinkMeshNodeObjectRole.SERVER,
            100,
            new ZLinkPlacementCapacity(
                new ZLinkCapacityUsage(actorActive, 1, 0),
                new ZLinkCapacityUsage(2, 3, 10),
                List.of(new ZLinkSpotTypeCapacity(
                    ZLinkPlacementObjectKind.INSTANCE_SPOT,
                    "room",
                    new ZLinkCapacityUsage(2, 1, 5)))),
            new ZLinkActivationConcurrency(activationActive, 8),
            List.of(),
            0,
            java.util.Optional.empty());
    }

    private static final class FakeNode implements ZLinkInternalMeshNode {
        private final RoutingId local = RoutingId.from("local");
        private volatile long maxMessageSize;
        private volatile int channelWeight = 7;
        private volatile int placementWeight = 100;
        private final MeshNodeStatus status = new MeshNodeStatus(
            MeshNodeState.READY,
            local,
            "mesh",
            "inproc://mesh",
            3,
            5,
            1,
            1,
            1,
            0,
            2,
            1,
            64,
            0,
            10);

        @Override
        public String name() {
            return "mesh";
        }

        @Override
        public void setBind(String endpoint) {
        }

        @Override
        public void addChannel(String channelName) {
        }

        @Override
        public void setChannelWeight(String channelName, int weight) {
            channelWeight = weight;
        }

        @Override
        public int placementWeight() {
            return placementWeight;
        }

        @Override
        public void setPlacementWeight(int weight) {
            placementWeight = weight;
        }

        @Override
        public long maxMessageSize() {
            return maxMessageSize;
        }

        @Override
        public void setMaxMessageSize(long value) {
            maxMessageSize = value;
        }

        @Override
        public void setRoutingId(RoutingId routingId) {
        }

        @Override
        public void start() {
        }

        @Override
        public long connectPeer(String endpoint) {
            return 1;
        }

        @Override
        public long connectPeer(String endpoint, RoutingId expectedRoutingId) {
            return 1;
        }

        @Override
        public MeshNodeStatus status() {
            return status;
        }

        @Override
        public List<MeshPeerEntry> peers() {
            return List.of(new MeshPeerEntry(
                RoutingId.from("peer"),
                "inproc://peer",
                1,
                MeshPeerSource.MANUAL,
                MeshPeerState.ADMITTED,
                4,
                8,
                1,
                0,
                10));
        }

        @Override
        public PeerChannels peerChannels(RoutingId peerRid, long lifecycleGeneration) {
            return new PeerChannels(List.of("channel"), List.of(3));
        }

        @Override
        public Map<String, Integer> channelWeights() {
            return Map.of("channel", channelWeight);
        }

        @Override
        public List<Long> connectionIntentIds() {
            return List.of(1L);
        }

        @Override
        public MeshNodeMonitor openMonitor() {
            return new MeshNodeMonitor() {
                @Override
                public MeshMonitorEvent recv(RecvFlags flags) {
                    return null;
                }

                @Override
                public MeshMonitorStatus status() {
                    return new MeshMonitorStatus(
                        MeshNodeState.READY, 1, 0, 0, 0, 0, 0, 0, 0, 0);
                }

                @Override
                public void close() {
                }
            };
        }

        @Override
        public void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver) {
        }

        @Override
        public void close() {
        }
    }
}
