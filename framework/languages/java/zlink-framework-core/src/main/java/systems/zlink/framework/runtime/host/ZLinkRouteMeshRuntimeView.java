package systems.zlink.framework.runtime.host;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.Flow;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkCapacityUsage;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkPlacementObjectKind;
import systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot;
import systems.zlink.framework.monitoring.ZLinkPeerState;
import systems.zlink.framework.monitoring.ZLinkPlacementSnapshot;
import systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime;
import systems.zlink.framework.monitoring.ZLinkTopologyReason;
import systems.zlink.framework.monitoring.ZLinkTopologyState;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.binding.spot.MeshNodeState;
import systems.zlink.framework.runtime.internal.binding.spot.MeshPeerEntry;
import systems.zlink.framework.runtime.internal.binding.spot.PeerChannels;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkMeshNodeMonitoringProjection;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkStatusPublisher;

final class ZLinkRouteMeshRuntimeView implements ZLinkRouteMeshRuntime {
    private final ZLinkFrameworkRuntime runtime;
    private final AtomicLong sequence = new AtomicLong();

    ZLinkRouteMeshRuntimeView(ZLinkFrameworkRuntime runtime) {
        this.runtime = runtime;
    }

    @Override
    public ZLinkMeshNodeSnapshot snapshot(String meshName) {
        ZLinkInternalMeshNode node = requireNode(meshName);
        var nativeStatus = node.status();
        List<MeshPeerEntry> nativePeers = List.copyOf(node.peers());
        ZLinkTopologyState state = topologyState(nativeStatus.state());
        ZLinkFrameworkRuntimeState hostState = runtime.status().state();
        boolean locationStoreHealthy = locationStoreHealthy();
        if (hostState != ZLinkFrameworkRuntimeState.SERVING) {
            state = switch (hostState) {
                case PREPARING -> ZLinkTopologyState.STARTING;
                case SERVING -> state;
                case RELOCATING, RELOCATED, DRAINING ->
                    ZLinkTopologyState.STOPPING;
                case STOPPED -> ZLinkTopologyState.STOPPED;
                case ERROR -> ZLinkTopologyState.FAILED;
            };
        }
        if (state == ZLinkTopologyState.READY && !locationStoreHealthy) {
            state = ZLinkTopologyState.DEGRADED;
        }
        if (state == ZLinkTopologyState.READY
            && nativePeers.stream().anyMatch(
                ZLinkRouteMeshRuntimeView::requiredPeerUnavailable)) {
            state = ZLinkTopologyState.DEGRADED;
        }
        ZLinkMeshNodeMonitoringProjection placement =
            runtime.monitoringMeshNodeProjection(
                meshName, nativeStatus.routingId());
        List<ZLinkMeshChannelSnapshot> channels =
            runtime.monitoringMeshNodeChannelNames(meshName).stream()
                .distinct()
                .sorted()
                .map(channelName -> {
                    long readyTargets = nativePeers.stream()
                        .filter(peer -> peer.state()
                            == systems.zlink.framework.runtime.internal.binding.spot
                                .MeshPeerState.ADMITTED)
                        .map(peer -> peerChannels(node, peer))
                        .filter(peerChannels -> {
                            int index = peerChannels.names().indexOf(channelName);
                            return index >= 0
                                && peerChannels.weights().get(index) > 0;
                        })
                        .count();
                    return new ZLinkMeshChannelSnapshot(
                        channelName,
                        readyTargets > 0,
                        Math.toIntExact(readyTargets));
                })
                .toList();
        boolean hasAdmittedPeer = nativePeers.stream().anyMatch(peer ->
            peer.state()
                == systems.zlink.framework.runtime.internal.binding.spot.MeshPeerState.ADMITTED);
        if (state == ZLinkTopologyState.READY
            && hasAdmittedPeer
            && channels.stream().anyMatch(channel -> !channel.isReady())) {
            state = ZLinkTopologyState.DEGRADED;
        }
        boolean placementAvailable =
            state == ZLinkTopologyState.READY
                && placement.objectRole() == ZLinkMeshNodeObjectRole.SERVER
                && placement.placementWeight() > 0
                && hasActivationCapacity(placement)
                && hasAvailableObjectCapacity(placement);
        return new ZLinkMeshNodeSnapshot(
            meshName,
            state,
            state == ZLinkTopologyState.READY,
            Math.toIntExact(nativePeers.stream()
                .filter(peer -> peer.state()
                    == systems.zlink.framework.runtime.internal.binding.spot
                        .MeshPeerState.ADMITTED)
                .count()),
            channels,
            nativePeers.stream().map(ZLinkRouteMeshRuntimeView::peer).toList(),
            new ZLinkPlacementSnapshot(
                placementAvailable,
                placement.objectCapacity().actors().active(),
                placement.objectCapacity().spots().active(),
                placementAvailable
                    ? Optional.empty()
                    : Optional.of(
                        state == ZLinkTopologyState.STOPPING
                            ? ZLinkTopologyReason.DRAINING
                            : !locationStoreHealthy
                                ? ZLinkTopologyReason.LOCATION_UNAVAILABLE
                            : state != ZLinkTopologyState.READY
                                ? ZLinkTopologyReason.RUNTIME_NOT_READY
                                : ZLinkTopologyReason.CAPACITY_EXCEEDED)),
            sequence.incrementAndGet(),
            Instant.now());
    }

    @Override
    public Flow.Publisher<ZLinkMeshNodeSnapshot> observe(
        String meshName,
        int capacity) {
        requireNode(meshName);
        return ZLinkStatusPublisher.create(
            () -> snapshot(meshName),
            status -> List.of(
                status.state(),
                status.isReady(),
                status.readyPeerCount(),
                status.channels(),
                status.peers(),
                status.placement()),
            capacity);
    }

    @Override
    public boolean isReady(String meshName) {
        return snapshot(meshName).isReady();
    }

    private ZLinkInternalMeshNode requireNode(String meshName) {
        if (meshName == null || meshName.isBlank()) {
            throw new IllegalArgumentException("meshName is required");
        }
        ZLinkInternalMeshNode node =
            runtime.monitoringMeshNode(meshName);
        if (node == null) {
            throw new ZLinkConfigurationException(
                "RouteMesh is not configured: " + meshName);
        }
        return node;
    }

    private boolean locationStoreHealthy() {
        try {
            return runtime.monitoringLocationRuntimeQuery()
                .getStatus()
                .toCompletableFuture()
                .join()
                .storeHealthy();
        } catch (ZLinkConfigurationException notConfigured) {
            return true;
        } catch (RuntimeException unavailable) {
            return false;
        }
    }

    private static ZLinkMeshPeerSnapshot peer(MeshPeerEntry peer) {
        ZLinkPeerState state = switch (peer.state()) {
            case CONNECTING -> ZLinkPeerState.CONNECTING;
            case ADMITTED -> ZLinkPeerState.READY;
            case DRAINING -> ZLinkPeerState.DRAINING;
            case NOT_REQUIRED -> ZLinkPeerState.NOT_REQUIRED;
            default -> ZLinkPeerState.NOT_CONNECTED;
        };
        return new ZLinkMeshPeerSnapshot(
            peer.routingId(),
            state,
            state == ZLinkPeerState.READY || state == ZLinkPeerState.NOT_REQUIRED
                ? Optional.empty()
                : Optional.of(
                    state == ZLinkPeerState.DRAINING
                        ? ZLinkTopologyReason.DRAINING
                        : ZLinkTopologyReason.NO_READY_PEER));
    }

    private static PeerChannels peerChannels(
        ZLinkInternalMeshNode node,
        MeshPeerEntry peer) {
        try {
            return node.peerChannels(
                peer.routingId(), peer.lifecycleGeneration());
        } catch (RuntimeException ignored) {
            return new PeerChannels(List.of(), List.of());
        }
    }

    private static boolean requiredPeerUnavailable(MeshPeerEntry peer) {
        return peer.state()
            != systems.zlink.framework.runtime.internal.binding.spot
                .MeshPeerState.ADMITTED
            && peer.state()
                != systems.zlink.framework.runtime.internal.binding.spot
                    .MeshPeerState.NOT_REQUIRED;
    }

    private static ZLinkTopologyState topologyState(MeshNodeState state) {
        return switch (state) {
            case CREATED -> ZLinkTopologyState.STARTING;
            case STARTED, PARTIAL_READY, READY -> ZLinkTopologyState.READY;
            case DRAINING -> ZLinkTopologyState.STOPPING;
            case STOPPED -> ZLinkTopologyState.STOPPED;
            case ERROR -> ZLinkTopologyState.FAILED;
        };
    }

    private static boolean hasAvailableObjectCapacity(
        ZLinkMeshNodeMonitoringProjection placement) {
        boolean actorAvailable = placement.objectCapabilities().stream()
            .anyMatch(capability ->
                capability.objectKind() == ZLinkPlacementObjectKind.ACTOR)
            && hasRemainingCapacity(placement.objectCapacity().actors());
        boolean spotAvailable = placement.objectCapabilities().stream()
            .anyMatch(capability ->
                capability.objectKind() != ZLinkPlacementObjectKind.ACTOR)
            && hasRemainingCapacity(placement.objectCapacity().spots())
            && (placement.objectCapacity().spotTypes().isEmpty()
                || placement.objectCapacity().spotTypes().stream()
                    .anyMatch(type -> hasRemainingCapacity(type.usage())));
        return actorAvailable || spotAvailable;
    }

    private static boolean hasRemainingCapacity(ZLinkCapacityUsage capacity) {
        return capacity.limit() == 0
            || (long) capacity.active() + capacity.reserved() < capacity.limit();
    }

    private static boolean hasActivationCapacity(
        ZLinkMeshNodeMonitoringProjection placement) {
        int limit = placement.activationConcurrency().limit();
        return limit == 0
            || placement.activationConcurrency().active() < limit;
    }
}
