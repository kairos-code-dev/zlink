package systems.zlink.framework.runtime.internal.service;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

/**
 * Owns immutable peer snapshots and rejects updates from stale physical
 * connections.
 */
public final class ZLinkServiceTopologyRegistry {
    private final Map<RoutingId, Peer> peers = new HashMap<>();
    private final Map<String, Long> selectionCursors = new HashMap<>();
    private ZLinkServiceNodeDescriptor local;

    public ZLinkServiceTopologyRegistry(ZLinkServiceNodeDescriptor local) {
        this.local = Objects.requireNonNull(local, "local");
    }

    public synchronized ZLinkServiceNodeDescriptor localDescriptor() {
        return local;
    }

    public synchronized void publishLocal(ZLinkServiceNodeDescriptor descriptor) {
        Objects.requireNonNull(descriptor, "descriptor");
        if (!descriptor.meshName().equals(local.meshName())
            || !descriptor.nodeRoutingId().equals(local.nodeRoutingId())
            || descriptor.lifecycleGeneration() != local.lifecycleGeneration()) {
            throw new IllegalArgumentException("published descriptor changes local identity");
        }
        if (descriptor.descriptorRevision() <= local.descriptorRevision()) {
            throw new IllegalArgumentException(
                "published descriptor revision must increase");
        }
        if (!immutableFieldsMatch(local, descriptor)) {
            throw new IllegalArgumentException(
                "published descriptor changes immutable fields");
        }
        local = descriptor;
    }

    public synchronized AdmissionResult admit(
        ZLinkServiceNodeDescriptor descriptor,
        String connectionId) {
        return admit(
            descriptor,
            new Connection(
                connectionId,
                ZLinkServiceAdmissionGuard.ConnectionDirection.OUTBOUND,
                connectionId));
    }

    public synchronized AdmissionResult admit(
        ZLinkServiceNodeDescriptor descriptor,
        Connection connection) {
        String connectionId =
            connection == null ? null : connection.connectionId();
        if (descriptor == null || connectionId == null || connectionId.isBlank()) {
            return AdmissionResult.INVALID_DESCRIPTOR;
        }
        if (!descriptor.meshName().equals(local.meshName())) {
            return AdmissionResult.MESH_MISMATCH;
        }
        if (descriptor.nodeRoutingId().equals(local.nodeRoutingId())) {
            return AdmissionResult.INVALID_DESCRIPTOR;
        }
        Peer current = peers.get(descriptor.nodeRoutingId());
        if (current != null
            && current.connection().connectionId().equals(connectionId)) {
            return admitDescriptorUpdate(current, descriptor);
        }
        if (current != null
            && current.descriptor().lifecycleGeneration()
                == descriptor.lifecycleGeneration()
            && descriptor.descriptorRevision()
                > current.descriptor().descriptorRevision()
            && !immutableFieldsMatch(current.descriptor(), descriptor)) {
            return AdmissionResult.INVALID_DESCRIPTOR;
        }
        if (current != null
            && current.descriptor().lifecycleGeneration()
                == descriptor.lifecycleGeneration()
            && (current.descriptor().descriptorRevision()
                    > descriptor.descriptorRevision()
                || current.descriptor().descriptorRevision()
                        == descriptor.descriptorRevision()
                    && !current.descriptor().equals(descriptor))) {
            return AdmissionResult.STALE_DESCRIPTOR;
        }
        if (current != null
            && current.descriptor().lifecycleGeneration()
                == descriptor.lifecycleGeneration()) {
            var duplicate =
                ZLinkServiceAdmissionGuard.selectConnection(
                    local.nodeRoutingId(),
                    descriptor.nodeRoutingId(),
                    current.descriptor().lifecycleGeneration(),
                    current.connection().direction(),
                    current.connection().discriminator(),
                    descriptor.lifecycleGeneration(),
                    connection.direction(),
                    connection.discriminator());
            if (duplicate
                == ZLinkServiceAdmissionGuard
                    .DuplicateConnectionDecision.KEEP_CURRENT) {
                return AdmissionResult.DUPLICATE_REJECTED;
            }
        }
        peers.put(
            descriptor.nodeRoutingId(),
            new Peer(descriptor, connection));
        return AdmissionResult.ADMITTED;
    }

    private AdmissionResult admitDescriptorUpdate(
        Peer current,
        ZLinkServiceNodeDescriptor descriptor) {
        if (current.descriptor().lifecycleGeneration()
                != descriptor.lifecycleGeneration()
            || current.descriptor().descriptorRevision()
                > descriptor.descriptorRevision()
            || current.descriptor().descriptorRevision()
                    == descriptor.descriptorRevision()
                && !current.descriptor().equals(descriptor)) {
            return AdmissionResult.STALE_DESCRIPTOR;
        }
        if (!immutableFieldsMatch(current.descriptor(), descriptor)) {
            return AdmissionResult.INVALID_DESCRIPTOR;
        }
        peers.put(
            descriptor.nodeRoutingId(),
            new Peer(descriptor, current.connection()));
        return AdmissionResult.ADMITTED;
    }

    private static boolean immutableFieldsMatch(
        ZLinkServiceNodeDescriptor current,
        ZLinkServiceNodeDescriptor incoming) {
        if (!current.meshName().equals(incoming.meshName())
            || !current.nodeRoutingId().equals(incoming.nodeRoutingId())
            || current.lifecycleGeneration() != incoming.lifecycleGeneration()
            || !current.advertisedEndpoint().equals(
                incoming.advertisedEndpoint())
            || !current.securityIdentity().equals(incoming.securityIdentity())
            || current.effectiveMaxMessageBytes()
                != incoming.effectiveMaxMessageBytes()
            || current.applicationVersion() != incoming.applicationVersion()
            || !current.protocolCapabilities().equals(
                incoming.protocolCapabilities())
            || current.objectRole() != incoming.objectRole()
            || current.activeCapacityLimit() != incoming.activeCapacityLimit()
            || current.pendingCapacityLimit()
                != incoming.pendingCapacityLimit()
            || current.channels().size() != incoming.channels().size()) {
            return false;
        }
        for (int index = 0; index < current.channels().size(); index++) {
            if (!current.channels().get(index).name().equals(
                    incoming.channels().get(index).name())) {
                return false;
            }
        }
        return true;
    }

    public synchronized boolean disconnect(
        RoutingId nodeRoutingId,
        String connectionId) {
        Peer current = peers.get(nodeRoutingId);
        if (current == null
            || !current.connection().connectionId().equals(connectionId)) {
            return false;
        }
        peers.remove(nodeRoutingId);
        return true;
    }

    public synchronized Optional<Peer> peer(RoutingId nodeRoutingId) {
        return Optional.ofNullable(peers.get(nodeRoutingId));
    }

    public synchronized List<Peer> peers() {
        return peers.values().stream()
            .sorted(Comparator.comparing(
                peer -> peer.descriptor().nodeRoutingId().toString()))
            .toList();
    }

    public synchronized Optional<Peer> selectChannel(String channelName) {
        if (channelName == null || channelName.isBlank()) {
            throw new IllegalArgumentException("channelName is required");
        }
        List<WeightedPeer> eligible = new ArrayList<>();
        for (Peer peer : peers()) {
            for (ZLinkServiceNodeDescriptor.Channel channel
                : peer.descriptor().channels()) {
                if (channel.name().equals(channelName)
                    && channel.weight() > 0
                    && peer.descriptor().state()
                        == ZLinkServiceNodeDescriptor.State.SERVING) {
                    eligible.add(new WeightedPeer(peer, channel.weight()));
                    break;
                }
            }
        }
        return select("channel:" + channelName, eligible);
    }

    public synchronized Optional<Peer> selectPlacement() {
        return select(
            "placement",
            peers().stream()
                .filter(peer -> peer.descriptor().acceptsPlacement())
                .map(peer -> new WeightedPeer(
                    peer, peer.descriptor().placementWeight()))
                .toList());
    }

    private Optional<Peer> select(String key, List<WeightedPeer> eligible) {
        long total = 0;
        for (WeightedPeer candidate : eligible) {
            total = Math.addExact(total, candidate.weight());
        }
        if (total == 0) {
            return Optional.empty();
        }
        long cursor = selectionCursors.getOrDefault(key, 0L);
        selectionCursors.put(key, cursor == Long.MAX_VALUE ? 0L : cursor + 1);
        long selected = Math.floorMod(cursor, total);
        long offset = 0;
        for (WeightedPeer value : eligible) {
            offset = Math.addExact(offset, value.weight());
            if (selected < offset) {
                return Optional.of(value.peer());
            }
        }
        throw new IllegalStateException("weighted selection did not select a peer");
    }

    public record Peer(
        ZLinkServiceNodeDescriptor descriptor,
        Connection connection) {
        public Peer {
            Objects.requireNonNull(descriptor, "descriptor");
            Objects.requireNonNull(connection, "connection");
        }

        public String connectionId() {
            return connection.connectionId();
        }
    }

    public record Connection(
        String connectionId,
        ZLinkServiceAdmissionGuard.ConnectionDirection direction,
        String discriminator) {
        public Connection {
            Objects.requireNonNull(direction, "direction");
            if (connectionId == null || connectionId.isBlank()) {
                throw new IllegalArgumentException("connectionId is required");
            }
            if (discriminator == null || discriminator.isBlank()) {
                throw new IllegalArgumentException(
                    "connection discriminator is required");
            }
        }
    }

    public enum AdmissionResult {
        ADMITTED,
        MESH_MISMATCH,
        INVALID_DESCRIPTOR,
        STALE_DESCRIPTOR,
        DUPLICATE_REJECTED
    }

    private record WeightedPeer(Peer peer, int weight) {
    }
}
