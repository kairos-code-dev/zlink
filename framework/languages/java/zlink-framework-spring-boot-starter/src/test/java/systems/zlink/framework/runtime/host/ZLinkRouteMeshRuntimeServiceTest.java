package systems.zlink.framework.runtime.host;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Flow;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshMonitorEvent;
import systems.zlink.contracts.service.spot.MeshMonitorStatus;
import systems.zlink.contracts.service.spot.MeshNodeMonitor;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshPeerSource;
import systems.zlink.contracts.service.spot.MeshPeerState;
import systems.zlink.contracts.service.spot.PeerChannels;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.Drained;
import systems.zlink.framework.monitoring.ZLinkDrainControl;
import systems.zlink.framework.monitoring.ZLinkDrainResult;
import systems.zlink.framework.monitoring.ZLinkMeshDrained;
import systems.zlink.framework.monitoring.ZLinkMeshNodeState;
import systems.zlink.framework.monitoring.ZLinkMeshRuntimeEvent;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

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
    void drainDelegatesToHostControlAfterValidatingMeshName() {
        FakeNode node = new FakeNode();
        try (var runtime = runtime(node)) {
            assertInstanceOf(
                ZLinkMeshDrained.class,
                runtime.drain("mesh", Duration.ofSeconds(1))
                    .toCompletableFuture()
                    .join());
        }
    }

    @Test
    void multipleMeshesRejectHostGlobalDrainWithoutInvokingIt() {
        AtomicInteger drainCalls = new AtomicInteger();
        ZLinkDrainControl control = new ZLinkDrainControl() {
            @Override
            public java.util.concurrent.CompletionStage<ZLinkDrainResult> drain() {
                drainCalls.incrementAndGet();
                return CompletableFuture.completedFuture(new Drained());
            }

            @Override
            public java.util.concurrent.CompletionStage<ZLinkDrainResult> drain(
                Duration deadline) {
                return drain();
            }

            @Override
            public java.util.concurrent.CompletionStage<ZLinkDrainResult> awaitDrained() {
                drainCalls.incrementAndGet();
                return CompletableFuture.completedFuture(new Drained());
            }

            @Override
            public boolean isReady() {
                return true;
            }
        };
        try (var runtime = new ZLinkRouteMeshRuntimeService(
            () -> Map.of("mesh-a", new FakeNode(), "mesh-b", new FakeNode()),
            () -> {
                throw new ZLinkConfigurationException("not configured");
            },
            control)) {
            assertThrows(
                ZLinkConfigurationException.class,
                () -> runtime.drain("mesh-a", Duration.ofSeconds(1)));
            assertThrows(
                ZLinkConfigurationException.class,
                () -> runtime.awaitDrained("mesh-a"));
        }

        assertEquals(0, drainCalls.get());
    }

    @Test
    void runtimeOptionsApplyLiveMaxMessageSizeAndChannelWeight() {
        FakeNode node = new FakeNode();
        var options = new ZLinkRouteMeshRuntimeOptionsService(
            () -> Map.of("mesh", node));

        options.meshNode("mesh").maxMessageSize(4096);
        options.channel("mesh", "channel").weight(0);

        assertEquals(4096, options.meshNode("mesh").maxMessageSize());
        assertEquals(0, options.channel("mesh", "channel").weight());
    }

    private static ZLinkRouteMeshRuntimeService runtime(FakeNode node) {
        return new ZLinkRouteMeshRuntimeService(
            () -> Map.of("mesh", node),
            () -> {
                throw new ZLinkConfigurationException("not configured");
            },
            new ZLinkDrainControl() {
                @Override
                public java.util.concurrent.CompletionStage<ZLinkDrainResult> drain() {
                    return CompletableFuture.completedFuture(new Drained());
                }

                @Override
                public java.util.concurrent.CompletionStage<ZLinkDrainResult> drain(
                    Duration deadline) {
                    return drain();
                }

                @Override
                public java.util.concurrent.CompletionStage<ZLinkDrainResult> awaitDrained() {
                    return drain();
                }

                @Override
                public boolean isReady() {
                    return true;
                }
            });
    }

    private static final class FakeNode implements ZLinkInternalMeshNode {
        private final RoutingId local = RoutingId.from("local");
        private volatile long maxMessageSize;
        private volatile int channelWeight = 7;
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
            9,
            2,
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
                        MeshNodeState.READY, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
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
