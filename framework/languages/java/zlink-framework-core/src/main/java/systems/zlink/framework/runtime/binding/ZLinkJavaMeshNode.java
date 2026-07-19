package systems.zlink.framework.runtime.binding;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

final class ZLinkJavaMeshNode implements ZLinkInternalMeshNode {
    private final MeshNode node;
    private final String meshName;
    private final List<Long> connectionIntentIds = new ArrayList<>();
    private ZLinkJavaMeshDispatchPump dispatchPump;

    ZLinkJavaMeshNode(MeshNode node, String meshName) {
        this.node = node;
        this.meshName = meshName;
    }

    @Override
    public String name() {
        return meshName;
    }

    @Override
    public void setBind(String endpoint) {
        node.setBind(endpoint);
    }

    @Override
    public void addChannel(String channelName) {
        node.addChannel(channelName);
    }

    @Override
    public void setChannelWeight(String channelName, int weight) {
        node.setChannelWeight(channelName, weight);
    }

    @Override
    public void setRoutingId(RoutingId routingId) {
        node.setRoutingId(routingId);
    }

    @Override
    public void start() {
        node.start();
    }

    @Override
    public long connectPeer(String endpoint) {
        long intentId = node.connectPeer(endpoint);
        connectionIntentIds.add(intentId);
        return intentId;
    }

    @Override
    public long connectPeer(String endpoint, RoutingId expectedRoutingId) {
        long intentId = node.connectPeer(endpoint, expectedRoutingId);
        connectionIntentIds.add(intentId);
        return intentId;
    }

    @Override
    public MeshNodeStatus status() {
        return node.status();
    }

    @Override
    public List<MeshPeerEntry> peers() {
        return node.peers();
    }

    @Override
    public List<Long> connectionIntentIds() {
        return List.copyOf(connectionIntentIds);
    }

    @Override
    public void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver) {
        if (dispatchPump != null) {
            throw new IllegalStateException("MeshNode dispatch is already started: " + meshName);
        }
        dispatchPump = new ZLinkJavaMeshDispatchPump(
            new ZLinkJavaMeshDispatchSource(node),
            receiver);
    }

    @Override
    public void close() {
        if (dispatchPump != null) {
            dispatchPump.close();
            dispatchPump = null;
        }
        node.close();
    }
}
