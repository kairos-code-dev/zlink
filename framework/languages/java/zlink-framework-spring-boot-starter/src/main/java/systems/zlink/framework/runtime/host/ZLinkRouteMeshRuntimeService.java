package systems.zlink.framework.runtime.host;

import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Flow;
import java.util.concurrent.ForkJoinPool;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.MeshMonitorEvent;
import systems.zlink.contracts.service.spot.MeshMonitorEventKind;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshPeerState;
import systems.zlink.contracts.service.spot.PeerChannels;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.Drained;
import systems.zlink.framework.monitoring.ForceStopped;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshDrainResult;
import systems.zlink.framework.monitoring.ZLinkMeshDrainSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshDrained;
import systems.zlink.framework.monitoring.ZLinkMeshForceStopped;
import systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshNodeState;
import systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot;
import systems.zlink.framework.monitoring.ZLinkMeshRuntimeEvent;
import systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime;
import systems.zlink.framework.monitoring.ZLinkDrainControl;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;

public final class ZLinkRouteMeshRuntimeService implements ZLinkRouteMeshRuntime, AutoCloseable {
    private static final long MONITOR_IDLE_NANOS = 10_000_000L;

    private final Supplier<Map<String, ZLinkInternalMeshNode>> nodes;
    private final Supplier<ZLinkLocationRuntimeQuery> locationRuntime;
    private final ZLinkDrainControl drainControl;
    private final Map<String, AtomicLong> sequences = new ConcurrentHashMap<>();
    private final Map<String, MonitorHub> monitorHubs = new ConcurrentHashMap<>();
    private final AtomicReference<Instant> lastLocationFailure = new AtomicReference<>();
    private volatile boolean locationHealthy = true;

    public ZLinkRouteMeshRuntimeService(ZLinkFrameworkLifecycle lifecycle) {
        this(
            lifecycle::monitoringSpotSources,
            lifecycle::monitoringLocationRuntimeQuery,
            lifecycle);
    }

    ZLinkRouteMeshRuntimeService(
        Supplier<Map<String, ZLinkInternalMeshNode>> nodes,
        Supplier<ZLinkLocationRuntimeQuery> locationRuntime,
        ZLinkDrainControl drainControl) {
        this.nodes = java.util.Objects.requireNonNull(nodes, "nodes");
        this.locationRuntime =
            java.util.Objects.requireNonNull(locationRuntime, "locationRuntime");
        this.drainControl = java.util.Objects.requireNonNull(drainControl, "drainControl");
    }

    @Override
    public ZLinkMeshNodeSnapshot snapshot(String meshName) {
        ZLinkInternalMeshNode node = requireNode(meshName);
        var status = node.status();
        List<MeshPeerEntry> peers = node.peers();
        ZLinkMeshNodeState state = mapNodeState(status.state());
        List<ZLinkMeshChannelSnapshot> channels = node.channelWeights().entrySet().stream()
            .sorted(Map.Entry.comparingByKey())
            .map(channel -> {
                long readyMembers = peers.stream()
                    .filter(peer -> peer.state() == MeshPeerState.ADMITTED)
                    .map(peer -> peerChannels(node, peer))
                    .filter(peerChannels -> {
                        int index = peerChannels.names().indexOf(channel.getKey());
                        return index >= 0 && peerChannels.weights().get(index) > 0;
                    })
                    .count()
                    + (channel.getValue() > 0 ? 1 : 0);
                return new ZLinkMeshChannelSnapshot(
                    channel.getKey(), channel.getValue(), readyMembers, readyMembers > 0);
            })
            .toList();
        return new ZLinkMeshNodeSnapshot(
            meshName,
            status.routingId(),
            status.lifecycleGeneration(),
            status.descriptorRevision(),
            status.localEndpoint(),
            state,
            nextSequence(meshName),
            Instant.now(),
            descriptorSources(peers),
            peers.stream().map(peer -> mapPeer(node, peer)).toList(),
            channels,
            new ZLinkMeshClaimSnapshot(
                state == ZLinkMeshNodeState.SERVING,
                status.pendingApplicationMessages(),
                state == ZLinkMeshNodeState.SERVING || state == ZLinkMeshNodeState.DRAINING,
                status.pendingInfrastructureMessages()),
            locationSnapshot(),
            new ZLinkMeshDrainSnapshot(
                state,
                Optional.empty(),
                state == ZLinkMeshNodeState.DRAINED || state == ZLinkMeshNodeState.STOPPED,
                status.pendingApplicationMessages(),
                0,
                0));
    }

    @Override
    public Flow.Publisher<ZLinkMeshRuntimeEvent> observe(String meshName, int capacity) {
        if (capacity <= 0) {
            throw new IllegalArgumentException("capacity must be positive");
        }
        requireNode(meshName);
        return subscriber -> {
            if (subscriber == null) {
                throw new NullPointerException("subscriber");
            }
            MonitorHub hub = monitorHubs.computeIfAbsent(
                meshName, ignored -> new MonitorHub(meshName, requireNode(meshName)));
            hub.subscribe(subscriber, capacity);
        };
    }

    @Override
    public boolean isReady(String meshName) {
        return switch (requireNode(meshName).status().state()) {
            case STARTED, PARTIAL_READY, READY -> true;
            default -> false;
        };
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkMeshDrainResult> drain(
        String meshName,
        java.time.Duration deadline) {
        requireHostScopedDrainSafe(meshName);
        return drainControl.drain(deadline)
            .thenApply(ZLinkRouteMeshRuntimeService::mapDrainResult);
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkMeshDrainResult> awaitDrained(
        String meshName) {
        requireHostScopedDrainSafe(meshName);
        return drainControl.awaitDrained()
            .thenApply(ZLinkRouteMeshRuntimeService::mapDrainResult);
    }

    @Override
    public void close() {
        new ArrayList<>(monitorHubs.values()).forEach(MonitorHub::close);
        monitorHubs.clear();
    }

    private ZLinkInternalMeshNode requireNode(String meshName) {
        if (meshName == null || meshName.isBlank()) {
            throw new IllegalArgumentException("meshName is required");
        }
        ZLinkInternalMeshNode node = nodes.get().get(meshName);
        if (node == null) {
            throw new ZLinkConfigurationException(
                "RouteMesh is not configured: " + meshName);
        }
        return node;
    }

    private ZLinkInternalMeshNode requireHostScopedDrainSafe(String meshName) {
        ZLinkInternalMeshNode node = requireNode(meshName);
        if (nodes.get().size() != 1) {
            throw new ZLinkConfigurationException(
                "mesh-scoped drain is unavailable when a host contains multiple RouteMesh nodes");
        }
        return node;
    }

    private ZLinkLocationRuntimeSnapshot locationSnapshot() {
        try {
            var status = locationRuntime.get()
                .getStatus()
                .toCompletableFuture()
                .join();
            if (!status.storeHealthy() && locationHealthy) {
                Instant failureAt = Instant.now();
                if (status.lastRefreshAt() != null
                    && !failureAt.isAfter(status.lastRefreshAt())) {
                    failureAt = status.lastRefreshAt().plusNanos(1);
                }
                lastLocationFailure.set(failureAt);
            }
            locationHealthy = status.storeHealthy();
            Instant lastSuccessAt = status.lastRefreshAt();
            Instant failureAt = lastLocationFailure.get();
            if (status.storeHealthy()
                && failureAt != null
                && (lastSuccessAt == null || !lastSuccessAt.isAfter(failureAt))) {
                lastSuccessAt = failureAt.plusNanos(1);
            }
            return new ZLinkLocationRuntimeSnapshot(
                status.storeHealthy() ? "ready" : "degraded",
                Optional.ofNullable(lastSuccessAt),
                Optional.ofNullable(failureAt));
        } catch (RuntimeException ignored) {
            return new ZLinkLocationRuntimeSnapshot(
                "not_configured", Optional.empty(), Optional.empty());
        }
    }

    private long nextSequence(String meshName) {
        return sequences.computeIfAbsent(meshName, ignored -> new AtomicLong())
            .incrementAndGet();
    }

    private static ZLinkMeshDrainResult mapDrainResult(
        systems.zlink.framework.monitoring.ZLinkDrainResult result) {
        if (result instanceof Drained) {
            return new ZLinkMeshDrained();
        }
        if (result instanceof ForceStopped forced) {
            return new ZLinkMeshForceStopped(forced.reason());
        }
        throw new IllegalStateException("unknown drain result: " + result);
    }

    private static ZLinkMeshPeerSnapshot mapPeer(
        ZLinkInternalMeshNode node,
        MeshPeerEntry peer) {
        String admissionState;
        boolean ready;
        String drainState;
        switch (peer.state()) {
            case CONFIGURED -> {
                admissionState = "configured";
                ready = false;
                drainState = "serving";
            }
            case CONNECTING -> {
                admissionState = "connecting";
                ready = false;
                drainState = "serving";
            }
            case ADMITTED -> {
                admissionState = "ready";
                ready = true;
                drainState = "serving";
            }
            case DRAINING -> {
                admissionState = "draining";
                ready = false;
                drainState = "draining";
            }
            case CLOSED -> {
                admissionState = "disconnected";
                ready = false;
                drainState = "serving";
            }
            default -> {
                admissionState = "rejected";
                ready = false;
                drainState = "serving";
            }
        }
        return new ZLinkMeshPeerSnapshot(
            peer.routingId(),
            peer.lifecycleGeneration(),
            peer.descriptorRevision(),
            peer.endpoint(),
            admissionState,
            ready,
            drainState,
            peerChannels(node, peer).names(),
            peer.lastError() == 0
                ? Optional.empty()
                : Optional.of("errno " + peer.lastError()));
    }

    private static PeerChannels peerChannels(
        ZLinkInternalMeshNode node,
        MeshPeerEntry peer) {
        if (peer.state() != MeshPeerState.ADMITTED
            && peer.state() != MeshPeerState.DRAINING) {
            return new PeerChannels(List.of(), List.of());
        }
        try {
            return node.peerChannels(
                peer.routingId(), peer.lifecycleGeneration());
        } catch (RuntimeException ignored) {
            // The peer may have changed generation between peers() and this query.
            return new PeerChannels(List.of(), List.of());
        }
    }

    private static List<String> descriptorSources(List<MeshPeerEntry> peers) {
        boolean manual = peers.stream().anyMatch(peer ->
            peer.source() == systems.zlink.contracts.service.spot.MeshPeerSource.MANUAL
                || peer.source() == systems.zlink.contracts.service.spot.MeshPeerSource.MIXED);
        boolean redis = peers.stream().anyMatch(peer ->
            peer.source() == systems.zlink.contracts.service.spot.MeshPeerSource.DISCOVERY
                || peer.source() == systems.zlink.contracts.service.spot.MeshPeerSource.MIXED);
        if (manual && redis) {
            return List.of("manual_and_redis");
        }
        if (manual) {
            return List.of("manual");
        }
        return redis ? List.of("redis") : List.of();
    }

    private static ZLinkMeshNodeState mapNodeState(MeshNodeState state) {
        return switch (state) {
            case CREATED -> ZLinkMeshNodeState.STARTING;
            case STARTED, PARTIAL_READY, READY -> ZLinkMeshNodeState.SERVING;
            case DRAINING -> ZLinkMeshNodeState.DRAINING;
            case STOPPED -> ZLinkMeshNodeState.STOPPED;
            case ERROR -> ZLinkMeshNodeState.FAULTED;
        };
    }

    private List<ZLinkMeshRuntimeEvent> mapEvents(
        String meshName,
        RoutingId sourceRid,
        MeshMonitorEvent event) {
        String identifier = switch (event.kind()) {
            case STATE_CHANGED -> "zlink.runtime.mesh_node.state_changed";
            case CHANNEL_CHANGED -> "zlink.runtime.mesh_node.channel_changed";
            case CLAIM_REVOKED -> "zlink.runtime.mesh_node.claim_changed";
            case PEER_CONNECTING, PEER_ADMITTED, PEER_DRAINING, PEER_CLOSED,
                PEER_REJECTED, PROTOCOL_ERROR -> "zlink.runtime.mesh_node.peer_changed";
            default -> null;
        };
        if (identifier == null) {
            return List.of();
        }
        String reason = switch (event.kind()) {
            case PEER_CONNECTING -> "connecting";
            case PEER_ADMITTED -> "ready";
            case PEER_DRAINING -> "draining";
            case PEER_CLOSED -> "disconnected";
            case PEER_REJECTED -> "HandshakeFailed";
            case PROTOCOL_ERROR -> "rejected";
            case BACKPRESSURED -> "backpressure";
            default -> null;
        };
        ZLinkMeshRuntimeEvent mapped = new ZLinkMeshRuntimeEvent(
            identifier,
            nextSequence(meshName),
            Instant.now(),
            meshName,
            sourceRid,
            optionalRid(event.peerRid()),
            optionalPositive(event.peerLifecycleGeneration()),
            optionalPositive(event.peerDescriptorRevision()),
            optionalText(event.channelName()),
            event.kind() == MeshMonitorEventKind.CLAIM_REVOKED
                ? Optional.of("application")
                : Optional.empty(),
            Optional.empty(),
            Optional.ofNullable(reason),
            event.kind() == MeshMonitorEventKind.STATE_CHANGED
                ? Optional.of(mapNodeState(event.meshState()))
                : Optional.empty());
        if (event.kind() != MeshMonitorEventKind.STATE_CHANGED) {
            return List.of(mapped);
        }
        return List.of(
            mapped,
            new ZLinkMeshRuntimeEvent(
                "zlink.runtime.mesh_node.drain_changed",
                nextSequence(meshName),
                mapped.timestamp(),
                meshName,
                sourceRid,
                mapped.peerRid(),
                mapped.lifecycleGeneration(),
                mapped.descriptorRevision(),
                mapped.channelName(),
                mapped.claimDomain(),
                mapped.messageKind(),
                mapped.reason(),
                mapped.state()));
    }

    private static Optional<RoutingId> optionalRid(RoutingId value) {
        return value == null || value.size() == 0 ? Optional.empty() : Optional.of(value);
    }

    private static Optional<Long> optionalPositive(long value) {
        return value == 0 ? Optional.empty() : Optional.of(value);
    }

    private static Optional<String> optionalText(String value) {
        return value == null || value.isEmpty() ? Optional.empty() : Optional.of(value);
    }

    private final class MonitorHub implements AutoCloseable {
        private final String meshName;
        private final ZLinkInternalMeshNode node;
        private final Object gate = new Object();
        private final List<ObserverSubscription> observers = new ArrayList<>();
        private volatile boolean stopped;
        private Thread pump;

        MonitorHub(String meshName, ZLinkInternalMeshNode node) {
            this.meshName = meshName;
            this.node = node;
        }

        void subscribe(Flow.Subscriber<? super ZLinkMeshRuntimeEvent> subscriber, int capacity) {
            ObserverSubscription observer =
                new ObserverSubscription(this, subscriber, capacity);
            synchronized (gate) {
                if (stopped) {
                    subscriber.onSubscribe(observer);
                    observer.fail(new IllegalStateException("RouteMesh monitor is closed"));
                    return;
                }
                observers.add(observer);
                subscriber.onSubscribe(observer);
                var status = node.status();
                observer.enqueue(event(
                    "zlink.runtime.mesh_node.state_changed",
                    meshName,
                    status.routingId(),
                    Optional.of(mapNodeState(status.state()))));
                if (pump == null) {
                    pump = Thread.ofVirtual()
                        .name("zlink-mesh-monitor-" + meshName)
                        .start(this::pump);
                }
            }
        }

        void remove(ObserverSubscription observer) {
            boolean empty;
            synchronized (gate) {
                observers.remove(observer);
                empty = observers.isEmpty();
            }
            if (empty) {
                close();
                monitorHubs.remove(meshName, this);
            }
        }

        private void pump() {
            systems.zlink.contracts.service.spot.MeshNodeMonitor monitor = null;
            try {
                try {
                    monitor = node.openMonitor();
                } catch (RuntimeException ignored) {
                    // Snapshot polling remains authoritative when another monitor owns
                    // the Core push handle.
                }
                MeshNodeState previousState = node.status().state();
                List<MeshPeerEntry> previousPeers = List.copyOf(node.peers());
                String previousChannels = channelSignature(node, previousPeers);
                String previousLocationState = locationSnapshot().state();
                while (!stopped) {
                    MeshMonitorEvent nativeEvent = null;
                    if (monitor != null) {
                        try {
                            nativeEvent = monitor.recv(RecvFlags.DONT_WAIT);
                        } catch (RuntimeException ignored) {
                            try {
                                monitor.close();
                            } catch (RuntimeException closeFailure) {
                                ignored.addSuppressed(closeFailure);
                            }
                            monitor = null;
                        }
                    }
                    if (nativeEvent == null) {
                        var status = node.status();
                        List<MeshPeerEntry> peers = List.copyOf(node.peers());
                        String channels = channelSignature(node, peers);
                        String locationState = locationSnapshot().state();
                        List<ZLinkMeshRuntimeEvent> derived = new ArrayList<>(4);
                        if (status.state() != previousState) {
                            derived.add(event(
                                "zlink.runtime.mesh_node.state_changed",
                                meshName,
                                status.routingId(),
                                Optional.of(mapNodeState(status.state()))));
                        }
                        if (!peers.equals(previousPeers)) {
                            derived.add(peerChangedEvent(
                                meshName,
                                status.routingId(),
                                changedPeer(previousPeers, peers)));
                        }
                        if (!channels.equals(previousChannels)) {
                            derived.add(event(
                                "zlink.runtime.mesh_node.channel_changed",
                                meshName,
                                status.routingId(),
                                Optional.empty()));
                        }
                        if (!locationState.equals(previousLocationState)) {
                            derived.add(locationChangedEvent(
                                meshName,
                                status.routingId(),
                                locationState));
                        }
                        publish(derived);
                        previousState = status.state();
                        previousPeers = peers;
                        previousChannels = channels;
                        previousLocationState = locationState;
                        LockSupport.parkNanos(MONITOR_IDLE_NANOS);
                        continue;
                    }
                    RoutingId sourceRid = node.status().routingId();
                    List<ZLinkMeshRuntimeEvent> events =
                        mapEvents(meshName, sourceRid, nativeEvent);
                    publish(events);
                    previousState = node.status().state();
                    previousPeers = List.copyOf(node.peers());
                    previousChannels = channelSignature(node, previousPeers);
                    previousLocationState = locationSnapshot().state();
                }
            } catch (RuntimeException failure) {
                ObserverSubscription[] current;
                synchronized (gate) {
                    stopped = true;
                    current = observers.toArray(ObserverSubscription[]::new);
                    observers.clear();
                }
                for (ObserverSubscription observer : current) {
                    observer.fail(failure);
                }
            } finally {
                if (monitor != null) {
                    monitor.close();
                }
            }
        }

        private void publish(List<ZLinkMeshRuntimeEvent> events) {
            if (events.isEmpty()) {
                return;
            }
            ObserverSubscription[] current;
            synchronized (gate) {
                current = observers.toArray(ObserverSubscription[]::new);
            }
            for (ZLinkMeshRuntimeEvent event : events) {
                for (ObserverSubscription observer : current) {
                    observer.enqueue(event);
                }
            }
        }

        @Override
        public void close() {
            ObserverSubscription[] currentObservers;
            synchronized (gate) {
                if (stopped) {
                    return;
                }
                stopped = true;
                currentObservers = observers.toArray(ObserverSubscription[]::new);
                observers.clear();
            }
            Thread current = pump;
            if (current != null) {
                current.interrupt();
            }
            for (ObserverSubscription observer : currentObservers) {
                observer.complete();
            }
        }
    }

    private static MeshPeerEntry changedPeer(
        List<MeshPeerEntry> previous,
        List<MeshPeerEntry> current) {
        for (MeshPeerEntry peer : current) {
            if (!previous.contains(peer)) {
                return peer;
            }
        }
        for (MeshPeerEntry peer : previous) {
            if (!current.contains(peer)) {
                return peer;
            }
        }
        return current.isEmpty() ? null : current.getFirst();
    }

    private ZLinkMeshRuntimeEvent peerChangedEvent(
        String meshName,
        RoutingId sourceRid,
        MeshPeerEntry peer) {
        return new ZLinkMeshRuntimeEvent(
            "zlink.runtime.mesh_node.peer_changed",
            nextSequence(meshName),
            Instant.now(),
            meshName,
            sourceRid,
            peer == null ? Optional.empty() : Optional.of(peer.routingId()),
            peer == null ? Optional.empty() : Optional.of(peer.lifecycleGeneration()),
            peer == null ? Optional.empty() : Optional.of(peer.descriptorRevision()),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            peer == null ? Optional.empty() : Optional.of(peer.state().name().toLowerCase()),
            Optional.empty());
    }

    private static String channelSignature(
        ZLinkInternalMeshNode node,
        List<MeshPeerEntry> peers) {
        StringBuilder signature = new StringBuilder(node.channelWeights().toString());
        for (MeshPeerEntry peer : peers) {
            signature.append('|')
                .append(peer.routingId().toHex())
                .append(':')
                .append(peer.lifecycleGeneration())
                .append(':')
                .append(peerChannels(node, peer));
        }
        return signature.toString();
    }

    private ZLinkMeshRuntimeEvent event(
        String identifier,
        String meshName,
        RoutingId sourceRid,
        Optional<ZLinkMeshNodeState> state) {
        return new ZLinkMeshRuntimeEvent(
            identifier,
            nextSequence(meshName),
            Instant.now(),
            meshName,
            sourceRid,
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            state);
    }

    private ZLinkMeshRuntimeEvent locationChangedEvent(
        String meshName,
        RoutingId sourceRid,
        String state) {
        return new ZLinkMeshRuntimeEvent(
            "zlink.runtime.location.store_changed",
            nextSequence(meshName),
            Instant.now(),
            meshName,
            sourceRid,
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.empty(),
            Optional.of(state),
            Optional.empty());
    }

    private static final class ObserverSubscription implements Flow.Subscription {
        private final MonitorHub hub;
        private final Flow.Subscriber<? super ZLinkMeshRuntimeEvent> subscriber;
        private final int capacity;
        private final ArrayDeque<ZLinkMeshRuntimeEvent> pending = new ArrayDeque<>();
        private final AtomicLong demand = new AtomicLong();
        private final AtomicInteger draining = new AtomicInteger();
        private volatile boolean cancelled;

        ObserverSubscription(
            MonitorHub hub,
            Flow.Subscriber<? super ZLinkMeshRuntimeEvent> subscriber,
            int capacity) {
            this.hub = hub;
            this.subscriber = subscriber;
            this.capacity = capacity;
        }

        @Override
        public void request(long count) {
            if (count <= 0) {
                fail(new IllegalArgumentException("demand must be positive"));
                return;
            }
            demand.getAndUpdate(current -> {
                long updated = current + count;
                return updated < 0 ? Long.MAX_VALUE : updated;
            });
            scheduleDrain();
        }

        @Override
        public void cancel() {
            if (!cancelled) {
                cancelled = true;
                synchronized (pending) {
                    pending.clear();
                }
                hub.remove(this);
            }
        }

        void enqueue(ZLinkMeshRuntimeEvent event) {
            if (cancelled) {
                return;
            }
            synchronized (pending) {
                if (pending.size() == capacity) {
                    coalesceOrReplace(event);
                } else {
                    pending.addLast(event);
                }
            }
            scheduleDrain();
        }

        void fail(Throwable failure) {
            if (cancelled) {
                return;
            }
            cancelled = true;
            synchronized (pending) {
                pending.clear();
            }
            try {
                subscriber.onError(failure);
            } finally {
                hub.remove(this);
            }
        }

        void complete() {
            if (cancelled) {
                return;
            }
            cancelled = true;
            synchronized (pending) {
                pending.clear();
            }
            subscriber.onComplete();
        }

        private void scheduleDrain() {
            if (!cancelled && draining.compareAndSet(0, 1)) {
                ForkJoinPool.commonPool().execute(this::drain);
            }
        }

        private void drain() {
            try {
                while (!cancelled && demand.get() > 0) {
                    ZLinkMeshRuntimeEvent event;
                    synchronized (pending) {
                        event = pending.pollFirst();
                    }
                    if (event == null) {
                        return;
                    }
                    if (demand.get() != Long.MAX_VALUE) {
                        demand.decrementAndGet();
                    }
                    subscriber.onNext(event);
                }
            } catch (RuntimeException failure) {
                fail(failure);
            } finally {
                draining.set(0);
                synchronized (pending) {
                    if (!pending.isEmpty() && demand.get() > 0 && !cancelled) {
                        scheduleDrain();
                    }
                }
            }
        }

        private void coalesceOrReplace(ZLinkMeshRuntimeEvent event) {
            if (pending.stream().anyMatch(ObserverSubscription::isTerminalDrain)
                && !isTerminalDrain(event)) {
                return;
            }
            if (isTerminalDrain(event)) {
                boolean removed = pending.stream()
                    .filter(current -> !isTerminalDrain(current))
                    .findFirst()
                    .map(pending::remove)
                    .orElse(false);
                if (!removed) {
                    pending.pollFirst();
                }
            } else {
                pending.pollFirst();
            }
            pending.addLast(event);
        }

        private static boolean isTerminalDrain(ZLinkMeshRuntimeEvent event) {
            if (!event.identifier().equals("zlink.runtime.mesh_node.drain_changed")
                || event.state().isEmpty()) {
                return false;
            }
            return switch (event.state().get()) {
                case DRAINED, STOPPED, FORCE_STOPPING -> true;
                default -> false;
            };
        }

    }
}
