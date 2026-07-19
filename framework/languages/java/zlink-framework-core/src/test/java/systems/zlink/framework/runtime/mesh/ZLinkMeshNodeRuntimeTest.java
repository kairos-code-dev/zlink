package systems.zlink.framework.runtime.mesh;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

class ZLinkMeshNodeRuntimeTest {
    @Test
    void startAppliesIdentityTopologyAndPeersBeforeOwningLifecycle() {
        MeshNodeRegistration registration = new MeshNodeRegistration("game");
        registration.setRoutingId(RoutingId.from("game-1"));
        registration.listen("inproc://game-1");
        registration.channelName("orders").setWeight(2);
        registration.peerConnections().connect(
            RoutingId.from("game-2"),
            "inproc://game-2");

        RecordingMeshNode node = new RecordingMeshNode();
        try (ZLinkMeshNodeRuntime ignored = ZLinkMeshNodeRuntime.start(
            registration,
            (context, meshName) -> {
                assertEquals("game", meshName);
                return node;
            },
            new RecordingContext())) {
            assertEquals(List.of(
                "routing-id:game-1",
                "bind:inproc://game-1",
                "channel:orders",
                "weight:orders:2",
                "start",
                "peer:game-2:inproc://game-2"), node.calls);
        }

        assertEquals("close", node.calls.getLast());
    }

    private static final class RecordingContext implements ZLinkBackendContext {
        @Override public String name() { return "context"; }
        @Override public void shutdown() { }
        @Override public void close() { }
    }

    private static final class RecordingMeshNode implements ZLinkInternalMeshNode {
        private final List<String> calls = new ArrayList<>();

        @Override public String name() { return "mesh-node"; }
        @Override public void setBind(String endpoint) { calls.add("bind:" + endpoint); }
        @Override public void addChannel(String channelName) {
            calls.add("channel:" + channelName);
        }
        @Override public void setChannelWeight(String channelName, int weight) {
            calls.add("weight:" + channelName + ":" + weight);
        }
        @Override public void setRoutingId(RoutingId routingId) {
            calls.add("routing-id:" + routingId);
        }
        @Override public void start() { calls.add("start"); }
        @Override public long connectPeer(String endpoint) {
            calls.add("peer:" + endpoint);
            return 1L;
        }
        @Override public long connectPeer(String endpoint, RoutingId expectedRoutingId) {
            calls.add("peer:" + expectedRoutingId + ":" + endpoint);
            return 1L;
        }
        @Override public MeshNodeStatus status() {
            return new MeshNodeStatus(
                MeshNodeState.READY,
                RoutingId.from("game-1"),
                "game",
                "inproc://game-1",
                1L,
                1L,
                1,
                1,
                1,
                0,
                0L,
                0L,
                0L,
                0L,
                0L,
                0,
                0L);
        }
        @Override public List<MeshPeerEntry> peers() { return List.of(); }
        @Override public List<Long> connectionIntentIds() { return List.of(1L); }
        @Override public void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver) {
            calls.add("dispatch");
        }
        @Override public void close() { calls.add("close"); }
    }
}
