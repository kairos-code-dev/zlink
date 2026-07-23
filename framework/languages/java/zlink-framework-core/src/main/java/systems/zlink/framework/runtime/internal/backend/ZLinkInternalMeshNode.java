package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import java.util.Optional;
import java.time.Duration;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshNodeMonitor;
import systems.zlink.contracts.service.spot.PeerChannels;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;

public interface ZLinkInternalMeshNode extends ZLinkBackendObject {
    void setBind(String endpoint);

    void addChannel(String channelName);

    void setChannelWeight(String channelName, int weight);

    default long maxMessageSize() {
        return 0;
    }

    default void setMaxMessageSize(long value) {
        throw new UnsupportedOperationException("MeshNode max message size is not available");
    }

    default void setRouterHighWaterMark(int value) {
        // Optional for test and alternate backends that do not expose Core
        // RouteMesh admission yet.
    }

    default void setRouterSendTimeout(Duration value) {
        // Optional for test and alternate backends that do not expose Core
        // RouteMesh admission yet.
    }

    default void setRouterPendingAdmissionCapacity(int value) {
        // Optional for test and alternate backends that do not expose bounded
        // asynchronous RouteMesh admission yet.
    }

    default void setMailboxMessageBudget(long value) {
        // Optional for test and alternate backends that do not expose Core
        // mailbox admission yet.
    }

    void setRoutingId(RoutingId routingId);

    void start();

    long connectPeer(String endpoint);

    long connectPeer(String endpoint, RoutingId expectedRoutingId);

    default void removePeerConnection(long connectionIntentId) {
        // Optional for alternate and test backends that do not retain connection intents.
    }

    MeshNodeStatus status();

    List<MeshPeerEntry> peers();

    default PeerChannels peerChannels(RoutingId peerRid, long lifecycleGeneration) {
        return new PeerChannels(List.of(), List.of());
    }

    default java.util.Map<String, Integer> channelWeights() {
        return java.util.Map.of();
    }

    default MeshNodeMonitor openMonitor() {
        throw new UnsupportedOperationException("MeshNode monitor is not available");
    }

    List<Long> connectionIntentIds();

    void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver);

    default void setApplicationReceiver(ZLinkMeshApplicationReceiver receiver) {
        // Alternate backends may not support process-local Node direct dispatch.
    }

    default ZLinkInternalSpotNode spotNode() {
        throw new UnsupportedOperationException("MeshNode Spot backend is not available");
    }

    default Optional<RoutingId> selectPlacementTarget() {
        return Optional.empty();
    }
}
