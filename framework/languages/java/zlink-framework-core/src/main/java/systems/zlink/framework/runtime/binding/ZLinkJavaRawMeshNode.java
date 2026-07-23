package systems.zlink.framework.runtime.binding;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import java.util.concurrent.ThreadLocalRandom;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.MonitorEvent;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.service.spot.MeshNodeMonitor;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshPeerSource;
import systems.zlink.contracts.service.spot.MeshPeerState;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.OperationKind;
import systems.zlink.contracts.service.spot.OwnerKind;
import systems.zlink.contracts.service.spot.PeerChannels;
import systems.zlink.contracts.service.spot.PublishDetail;
import systems.zlink.contracts.service.spot.ReadyDomain;
import systems.zlink.contracts.service.spot.ReadyRecord;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshApplicationReceiver;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.protocol.ServiceWireConstants;
import systems.zlink.framework.runtime.service.ZLinkServiceLivenessRegistry;
import systems.zlink.framework.runtime.service.ZLinkServiceM6AWireCodec;
import systems.zlink.framework.runtime.service.ZLinkServiceM6BWireCodec;
import systems.zlink.framework.runtime.service.ZLinkServiceMailbox;
import systems.zlink.framework.runtime.service.ZLinkServiceNodeDescriptor;
import systems.zlink.framework.runtime.service.ZLinkServiceOperationRegistry;
import systems.zlink.framework.runtime.service.ZLinkServiceTopologyRegistry;
import systems.zlink.framework.runtime.service.ZLinkServiceWireCodec;
import systems.zlink.framework.runtime.service.ZLinkServiceWireFrame;

/**
 * Raw-binding RouteMesh owner. Service topology and application dispatch are
 * implemented in Framework code; the binding only owns ROUTER transport.
 */
final class ZLinkJavaRawMeshNode implements ZLinkInternalMeshNode {
    private static final int PREFIX_BYTES = 5;

    private final String meshName;
    private final ZLinkJavaRawServicePort port;
    private final Map<String, Integer> channelWeights = new ConcurrentHashMap<>();
    private final Map<Long, PeerIntent> peerIntents = new ConcurrentHashMap<>();
    private final Map<RoutingId, Map<String, Integer>> admittedPeerChannels =
        new ConcurrentHashMap<>();
    private final AtomicLong nextIntent = new AtomicLong(1);
    private final AtomicLong channelSelectionCursor = new AtomicLong();
    private final AtomicLong nextCorrelation = new AtomicLong(1);
    private final AtomicLong nextDispatchEnvelope = new AtomicLong(1);
    private final Map<Long, ZLinkMeshDispatchRecord> dispatchEnvelopes =
        new ConcurrentHashMap<>();
    private final AtomicBoolean closed = new AtomicBoolean();
    private final ZLinkServiceM6AWireCodec wire =
        new ZLinkServiceM6AWireCodec();
    private final ZLinkServiceM6BWireCodec statefulWire =
        new ZLinkServiceM6BWireCodec();
    private final ZLinkServiceLivenessRegistry liveness =
        new ZLinkServiceLivenessRegistry();
    private final ScheduledExecutorService deadlines =
        Executors.newSingleThreadScheduledExecutor(Thread.ofVirtual()
            .name("zlink-jvm-service-deadline-" + System.identityHashCode(this))
            .factory());
    private final ZLinkServiceOperationRegistry operations =
        new ZLinkServiceOperationRegistry(deadlines);
    private final java.util.concurrent.ConcurrentLinkedQueue<MonitorEvent>
        monitorEvents = new java.util.concurrent.ConcurrentLinkedQueue<>();
    private final Map<RoutingId, String> connectionIds =
        new ConcurrentHashMap<>();
    private final Map<RoutingId, Long> nextAnnouncementNanos =
        new ConcurrentHashMap<>();
    private volatile RoutingId routingId;
    private volatile String bindEndpoint;
    private volatile RouterSocket router;
    private volatile SocketMonitor rawMonitor;
    private volatile MeshNodeState state = MeshNodeState.CREATED;
    private volatile Consumer<ZLinkMeshDispatchRecord> receiver =
        ZLinkMeshDispatchRecord::close;
    private volatile ZLinkMeshApplicationReceiver applicationReceiver;
    private volatile ZLinkJavaRawSpotNode spotNode;
    private volatile ExecutorService pump;
    private volatile long mailboxMessageBudget = 4096;
    private volatile ZLinkServiceMailbox mailbox;
    private volatile ZLinkServiceTopologyRegistry topology;
    private volatile ZLinkServiceNodeDescriptor localDescriptor;

    ZLinkJavaRawMeshNode(Context context, String meshName) {
        if (meshName == null || meshName.isBlank()) {
            throw new IllegalArgumentException("meshName is required");
        }
        this.meshName = meshName;
        this.port = new ZLinkJavaRawServicePort(context);
    }

    @Override
    public String name() {
        return meshName;
    }

    @Override
    public void setBind(String endpoint) {
        requireCreated();
        if (endpoint == null || endpoint.isBlank()) {
            throw new IllegalArgumentException("bind endpoint is required");
        }
        bindEndpoint = endpoint;
    }

    @Override
    public void addChannel(String channelName) {
        requireCreated();
        channelWeights.putIfAbsent(requireChannel(channelName), 1);
    }

    @Override
    public void setChannelWeight(String channelName, int weight) {
        if (weight < 0 || weight > 100) {
            throw new IllegalArgumentException("channel weight must be in 0..100");
        }
        channelWeights.put(requireChannel(channelName), weight);
    }

    @Override
    public Map<String, Integer> channelWeights() {
        return Map.copyOf(channelWeights);
    }

    @Override
    public void setRoutingId(RoutingId value) {
        requireCreated();
        routingId = java.util.Objects.requireNonNull(value, "routingId");
    }

    @Override
    public void setMailboxMessageBudget(long value) {
        if (value < 0) {
            throw new IllegalArgumentException("mailbox budget must not be negative");
        }
        mailboxMessageBudget = value == 0 ? 4096 : value;
    }

    @Override
    public void start() {
        requireCreated();
        if (bindEndpoint == null) {
            throw new IllegalStateException("bind endpoint is required");
        }
        if (routingId == null) {
            routingId = RoutingId.from(UUID.randomUUID());
        }
        RouterSocket opened = port.openRouter(routingId);
        try {
            opened.bind(bindEndpoint);
            router = opened;
            long lifecycle = positiveRandomLong();
            List<ZLinkServiceNodeDescriptor.Channel> descriptorChannels =
                channelWeights.entrySet().stream()
                    .sorted(Map.Entry.comparingByKey())
                    .map(entry -> new ZLinkServiceNodeDescriptor.Channel(
                        entry.getKey(), entry.getValue()))
                    .toList();
            localDescriptor = descriptor(
                lifecycle,
                1,
                descriptorChannels,
                ZLinkServiceNodeDescriptor.State.PREPARING);
            topology = new ZLinkServiceTopologyRegistry(localDescriptor);
            mailbox = new ZLinkServiceMailbox(
                mailboxMessageBudget,
                64L * 1024 * 1024,
                1024,
                8L * 1024 * 1024);
            state = MeshNodeState.STARTED;
            localDescriptor = descriptor(
                lifecycle,
                2,
                descriptorChannels,
                ZLinkServiceNodeDescriptor.State.SERVING);
            topology.publishLocal(localDescriptor);
            state = MeshNodeState.READY;
            rawMonitor = port.openMonitor(
                opened,
                MonitorEventType.CONNECTION_READY,
                MonitorEventType.DISCONNECTED,
                MonitorEventType.CLOSED);
            rawMonitor.onEvent(monitorEvents::add);
            startPump();
        } catch (RuntimeException failure) {
            state = MeshNodeState.ERROR;
            throw failure;
        }
    }

    @Override
    public long connectPeer(String endpoint) {
        return connectPeer(endpoint, null);
    }

    @Override
    public long connectPeer(String endpoint, RoutingId expectedRoutingId) {
        RouterSocket current = requireStarted();
        if (endpoint == null || endpoint.isBlank()) {
            throw new IllegalArgumentException("peer endpoint is required");
        }
        current.connect(endpoint);
        long intent = nextIntent.getAndIncrement();
        peerIntents.put(
            intent,
            new PeerIntent(endpoint, expectedRoutingId, System.currentTimeMillis()));
        if (expectedRoutingId != null) {
            nextAnnouncementNanos.put(expectedRoutingId, 0L);
        }
        return intent;
    }

    @Override
    public void removePeerConnection(long connectionIntentId) {
        PeerIntent removed = peerIntents.remove(connectionIntentId);
        if (removed != null && router != null) {
            if (removed.expectedRoutingId() != null) {
                admittedPeerChannels.remove(removed.expectedRoutingId());
                nextAnnouncementNanos.remove(removed.expectedRoutingId());
                String connectionId =
                    connectionIds.remove(removed.expectedRoutingId());
                if (connectionId != null) {
                    ZLinkServiceTopologyRegistry currentTopology = topology;
                    if (currentTopology != null) {
                        currentTopology.disconnect(
                            removed.expectedRoutingId(), connectionId);
                    }
                    liveness.disconnect(
                        removed.expectedRoutingId(), connectionId);
                }
            }
            router.disconnect(removed.endpoint());
        }
    }

    @Override
    public MeshNodeStatus status() {
        return new MeshNodeStatus(
            state,
            routingId,
            meshName,
            bindEndpoint,
            1,
            1,
            channelWeights.size(),
            peerIntents.size(),
            admittedPeerChannels.size(),
            0,
            mailbox == null
                ? 0
                : mailbox.pendingMessages(
                    ZLinkServiceMailbox.Domain.APPLICATION),
            0,
            0,
            0,
            0,
            0,
            System.currentTimeMillis());
    }

    @Override
    public List<MeshPeerEntry> peers() {
        return peerIntents.entrySet().stream()
            .filter(entry -> entry.getValue().expectedRoutingId() != null)
            .sorted(Comparator.comparingLong(Map.Entry::getKey))
            .map(entry -> new MeshPeerEntry(
                entry.getValue().expectedRoutingId(),
                entry.getValue().endpoint(),
                entry.getKey(),
                MeshPeerSource.MANUAL,
                admittedPeerChannels.containsKey(
                    entry.getValue().expectedRoutingId())
                    ? MeshPeerState.ADMITTED
                    : MeshPeerState.CONNECTING,
                1,
                1,
                0,
                0,
                entry.getValue().createdAtMs()))
            .toList();
    }

    @Override
    public PeerChannels peerChannels(
        RoutingId peerRid,
        long lifecycleGeneration) {
        Map<String, Integer> channels =
            admittedPeerChannels.getOrDefault(peerRid, Map.of());
        List<Map.Entry<String, Integer>> ordered = channels.entrySet().stream()
            .sorted(Map.Entry.comparingByKey())
            .toList();
        return new PeerChannels(
            ordered.stream().map(Map.Entry::getKey).toList(),
            ordered.stream().map(Map.Entry::getValue).toList());
    }

    @Override
    public MeshNodeMonitor openMonitor() {
        return new ZLinkJavaRawMeshMonitor(this::status);
    }

    @Override
    public List<Long> connectionIntentIds() {
        return peerIntents.keySet().stream().sorted().toList();
    }

    @Override
    public void startDispatch(Consumer<ZLinkMeshDispatchRecord> value) {
        receiver = java.util.Objects.requireNonNull(value, "receiver");
    }

    @Override
    public void setApplicationReceiver(ZLinkMeshApplicationReceiver value) {
        applicationReceiver =
            java.util.Objects.requireNonNull(value, "applicationReceiver");
        ZLinkJavaRawSpotNode current = spotNode;
        if (current != null) {
            current.setApplicationReceiver(value);
        }
    }

    @Override
    public synchronized ZLinkInternalSpotNode spotNode() {
        if (spotNode == null) {
            spotNode = new ZLinkJavaRawSpotNode(this);
            if (applicationReceiver != null) {
                spotNode.setApplicationReceiver(applicationReceiver);
            }
        }
        return spotNode;
    }

    @Override
    public Optional<RoutingId> selectPlacementTarget() {
        ZLinkServiceTopologyRegistry current = topology;
        return current == null
            ? Optional.empty()
            : current.selectPlacement()
                .map(peer -> peer.descriptor().nodeRoutingId());
    }

    RoutingId routingId() {
        return routingId;
    }

    long lifecycleGeneration() {
        ZLinkServiceNodeDescriptor current = localDescriptor;
        if (current == null) {
            throw new IllegalStateException("raw MeshNode is not started");
        }
        return current.lifecycleGeneration();
    }

    boolean sendNode(
        RoutingId target,
        byte[] metadata,
        List<Message> parts,
        boolean request,
        Long correlation) {
        if (topology == null || topology.peer(target).isEmpty()) {
            return false;
        }
        List<byte[]> frames = new ArrayList<>();
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        frames.add(request
            ? wire.encodeNodeRequestHeader(
                java.util.Objects.requireNonNull(correlation, "correlation"),
                flags)
            : wire.encodeNodeSendHeader(flags));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        return port.send(requireStarted(), target, frames);
    }

    boolean sendChannel(
        String channelName,
        byte[] metadata,
        List<Message> parts) {
        String selectedChannel = requireChannel(channelName);
        ZLinkServiceTopologyRegistry currentTopology = topology;
        Optional<RoutingId> target = currentTopology == null
            ? Optional.empty()
            : currentTopology.selectChannel(selectedChannel)
                .map(peer -> peer.descriptor().nodeRoutingId());
        if (target.isEmpty()) {
            return false;
        }
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        List<byte[]> frames = new ArrayList<>();
        frames.add(wire.encodeChannelSendHeader(selectedChannel, flags));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        return port.send(requireStarted(), target.orElseThrow(), frames);
    }

    PublishDetail publishLogicalMulticast(
        ZLinkJavaRawSpot source,
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts) {
        String selectedChannel = requireChannel(channelName);
        ZLinkJavaRawSpotNode currentSpots =
            (ZLinkJavaRawSpotNode) spotNode();
        ZLinkJavaRawSpotNode.MulticastLocalDetail local =
            currentSpots.enqueueLogicalMulticast(
                selectedChannel,
                topic,
                source == null ? routingId : source.routingId(),
                routingId,
                metadata,
                parts);
        List<ZLinkServiceTopologyRegistry.Peer> targets =
            topology == null
                ? List.of()
                : topology.peers().stream()
                    .filter(peer -> peer.descriptor().channels().stream()
                        .anyMatch(channel ->
                            channel.name().equals(selectedChannel)))
                    .toList();
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        List<byte[]> frames = new ArrayList<>();
        frames.add(statefulWire.encodeLogicalMulticastHeader(
            flags,
            selectedChannel,
            topic,
            source == null ? routingId : source.routingId()));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        int admittedRemote = 0;
        int unreachableRemote = 0;
        for (ZLinkServiceTopologyRegistry.Peer target : targets) {
            if (port.send(
                requireStarted(),
                target.descriptor().nodeRoutingId(),
                frames)) {
                admittedRemote++;
            } else {
                unreachableRemote++;
            }
        }
        return new PublishDetail(
            targets.size(),
            admittedRemote,
            targets.size() - admittedRemote,
            unreachableRemote,
            local.snapshot(),
            local.admitted(),
            local.snapshot() - local.admitted());
    }

    boolean requestNode(
        RoutingId target,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        Duration timeout) {
        return request(
            target,
            metadata,
            parts,
            callback,
            timeout,
            null);
    }

    boolean requestChannel(
        String channelName,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        Duration timeout) {
        Optional<RoutingId> target = topology.selectChannel(requireChannel(channelName))
            .map(peer -> peer.descriptor().nodeRoutingId());
        return target.isPresent() && request(
            target.orElseThrow(),
            metadata,
            parts,
            callback,
            timeout,
            channelName);
    }

    boolean sendSpot(
        RoutingId sourceSpotRid,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long targetSpotGeneration,
        byte[] metadata,
        List<Message> parts) {
        Optional<ZLinkServiceTopologyRegistry.Peer> peer =
            topology == null ? Optional.empty() : topology.peer(targetNodeRid);
        long authorityOwnerGeneration =
            ((ZLinkJavaRawSpotNode) spotNode())
                .spotAuthorityOwnerGeneration(
                    targetNodeRid,
                    targetSpotRid,
                    targetSpotGeneration);
        if (peer.isEmpty()
            || targetSpotGeneration <= 0
            || authorityOwnerGeneration <= 0) {
            return false;
        }
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        List<byte[]> frames = new ArrayList<>();
        frames.add(statefulWire.encodeSpotHeader(
            false,
            flags,
            null,
            sourceSpotRid,
            new ZLinkServiceM6BWireCodec.SpotRouteFence(
                targetSpotRid,
                targetSpotGeneration,
                targetNodeRid,
                peer.orElseThrow().descriptor().lifecycleGeneration(),
                authorityOwnerGeneration)));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        return port.send(requireStarted(), targetNodeRid, frames);
    }

    boolean requestSpot(
        RoutingId sourceSpotRid,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long targetSpotGeneration,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        Duration timeout) {
        Optional<ZLinkServiceTopologyRegistry.Peer> peer =
            topology == null ? Optional.empty() : topology.peer(targetNodeRid);
        long authorityOwnerGeneration =
            ((ZLinkJavaRawSpotNode) spotNode())
                .spotAuthorityOwnerGeneration(
                    targetNodeRid,
                    targetSpotRid,
                    targetSpotGeneration);
        if (peer.isEmpty()
            || targetSpotGeneration <= 0
            || authorityOwnerGeneration <= 0) {
            return false;
        }
        java.util.Objects.requireNonNull(callback, "callback");
        long correlation = allocateCorrelation();
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        List<byte[]> frames = new ArrayList<>();
        frames.add(statefulWire.encodeSpotHeader(
            true,
            flags,
            correlation,
            sourceSpotRid,
            new ZLinkServiceM6BWireCodec.SpotRouteFence(
                targetSpotRid,
                targetSpotGeneration,
                targetNodeRid,
                peer.orElseThrow().descriptor().lifecycleGeneration(),
                authorityOwnerGeneration)));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        ZLinkServiceOperationRegistry.Operation<ZLinkBackendReceived> operation =
            operations.register(timeout);
        operation.completion().whenComplete((reply, failure) -> {
            if (failure == null) {
                callback.handle(reply);
                return;
            }
            callback.handle(new ZLinkBackendReceived(
                failure instanceof java.util.concurrent.TimeoutException
                    ? ZLinkBackendRequestResult.TIMED_OUT
                    : ZLinkBackendRequestResult.INTERNAL_ERROR,
                Optional.of(targetNodeRid),
                Optional.of(targetSpotRid),
                Optional.of(correlation),
                List.of()));
        });
        boolean submitted = port.request(
            requireStarted(),
            targetNodeRid,
            frames,
            timeout,
            (result, replyFrames) -> completeSpotRequest(
                operation.id(),
                targetNodeRid,
                targetSpotRid,
                correlation,
                result,
                replyFrames));
        if (!submitted) {
            operations.complete(
                operation.id(),
                new ZLinkBackendReceived(
                    ZLinkBackendRequestResult.NOT_CONNECTED,
                    Optional.of(targetNodeRid),
                    Optional.of(targetSpotRid),
                    Optional.of(correlation),
                    List.of()));
        }
        return submitted;
    }

    private void completeSpotRequest(
        UUID operationId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long correlation,
        systems.zlink.contracts.sockets.RequestResult result,
        List<byte[]> frames) {
        if (result != systems.zlink.contracts.sockets.RequestResult.OK) {
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    backendResult(result),
                    Optional.of(targetNodeRid),
                    Optional.of(targetSpotRid),
                    Optional.of(correlation),
                    List.of()));
            return;
        }
        try {
            if (frames.isEmpty() || frames.size() > 2) {
                throw new IllegalArgumentException(
                    "invalid Spot reply frame count");
            }
            ZLinkServiceM6AWireCodec.Reply header =
                wire.decodeReplyHeader(frames.getFirst());
            if (header.correlation() != correlation
                || (header.terminalResult() == 0) != (frames.size() == 2)) {
                throw new IllegalArgumentException(
                    "Spot reply terminal mismatch");
            }
            List<Message> replyParts = header.terminalResult() == 0
                ? List.of(Message.from(
                    wire.decodeApplicationPayload(frames.get(1)).payload()))
                : List.of();
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    header.terminalResult() == 0
                        ? ZLinkBackendRequestResult.OK
                        : backendResult(header.terminalResult()),
                    Optional.of(targetNodeRid),
                    Optional.of(targetSpotRid),
                    Optional.of(correlation),
                    replyParts));
        } catch (RuntimeException failure) {
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    ZLinkBackendRequestResult.PROTOCOL_ERROR,
                    Optional.of(targetNodeRid),
                    Optional.of(targetSpotRid),
                    Optional.of(correlation),
                    List.of()));
        }
    }

    boolean sendActor(
        systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor,
        List<Message> parts) {
        Optional<ZLinkServiceTopologyRegistry.Peer> peer =
            topology == null ? Optional.empty() : topology.peer(actor.nodeRid());
        long authorityOwnerGeneration =
            ((ZLinkJavaRawSpotNode) spotNode())
                .actorAuthorityOwnerGeneration(actor);
        if (peer.isEmpty()
            || actor.generation() <= 0
            || authorityOwnerGeneration <= 0) {
            return false;
        }
        List<byte[]> frames = List.of(
            statefulWire.encodeActorHeader(
                false,
                0,
                null,
                null,
                new ZLinkServiceM6BWireCodec.ActorRouteFence(
                    actor,
                    peer.orElseThrow().descriptor().lifecycleGeneration(),
                    authorityOwnerGeneration)),
            wire.encodeApplicationPayload(applicationPayload(parts)));
        return port.send(requireStarted(), actor.nodeRid(), frames);
    }

    boolean sendInstanceSpot(
        ZLinkServiceM6BWireCodec.InstanceRouteFence route,
        RoutingId sourceSpotRid,
        byte[] metadata,
        List<Message> parts) {
        Optional<ZLinkServiceTopologyRegistry.Peer> peer =
            topology == null
                ? Optional.empty()
                : topology.peer(route.targetNodeRid());
        if (peer.isEmpty()
            || peer.orElseThrow().descriptor().lifecycleGeneration()
                != route.targetNodeGeneration()
            || localDescriptor == null) {
            return false;
        }
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        List<byte[]> frames = new ArrayList<>();
        frames.add(statefulWire.encodeInstanceSpotHeader(
            new ZLinkServiceM6BWireCodec.InstanceSpotMessage(
                flags,
                route,
                localDescriptor.lifecycleGeneration(),
                routingId,
                sourceSpotRid,
                false,
                0,
                0,
                null)));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        return port.send(requireStarted(), route.targetNodeRid(), frames);
    }

    CompletionStage<List<Message>> requestActor(
        systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor,
        List<Message> parts,
        Duration timeout) {
        Optional<ZLinkServiceTopologyRegistry.Peer> peer =
            topology == null ? Optional.empty() : topology.peer(actor.nodeRid());
        long authorityOwnerGeneration =
            ((ZLinkJavaRawSpotNode) spotNode())
                .actorAuthorityOwnerGeneration(actor);
        if (peer.isEmpty()
            || actor.generation() <= 0
            || authorityOwnerGeneration <= 0) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("remote Actor route is not connected"));
        }
        long correlation = allocateCorrelation();
        List<byte[]> frames = List.of(
            statefulWire.encodeActorHeader(
                true,
                0,
                correlation,
                null,
                new ZLinkServiceM6BWireCodec.ActorRouteFence(
                    actor,
                    peer.orElseThrow().descriptor().lifecycleGeneration(),
                    authorityOwnerGeneration)),
            wire.encodeApplicationPayload(applicationPayload(parts)));
        ZLinkServiceOperationRegistry.Operation<ZLinkBackendReceived> operation =
            operations.register(timeout);
        boolean submitted = port.request(
            requireStarted(),
            actor.nodeRid(),
            frames,
            timeout,
            (result, replyFrames) -> completeActorRequest(
                operation.id(),
                actor,
                correlation,
                result,
                replyFrames));
        if (!submitted) {
            operations.complete(
                operation.id(),
                new ZLinkBackendReceived(
                    ZLinkBackendRequestResult.NOT_CONNECTED,
                    Optional.of(actor.nodeRid()),
                    Optional.empty(),
                    Optional.of(correlation),
                    List.of()));
        }
        return operation.completion().thenCompose(received -> {
            if (received.result() != ZLinkBackendRequestResult.OK) {
                received.close();
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "Actor request failed: " + received.result()));
            }
            return CompletableFuture.completedFuture(received.parts());
        });
    }

    private void completeActorRequest(
        UUID operationId,
        systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor,
        long correlation,
        systems.zlink.contracts.sockets.RequestResult result,
        List<byte[]> frames) {
        if (result != systems.zlink.contracts.sockets.RequestResult.OK) {
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    backendResult(result),
                    Optional.of(actor.nodeRid()),
                    Optional.empty(),
                    Optional.of(correlation),
                    List.of()));
            return;
        }
        try {
            if (frames.isEmpty() || frames.size() > 2) {
                throw new IllegalArgumentException(
                    "invalid Actor reply frame count");
            }
            ZLinkServiceM6AWireCodec.Reply header =
                wire.decodeReplyHeader(frames.getFirst());
            if (header.correlation() != correlation
                || (header.terminalResult() == 0) != (frames.size() == 2)) {
                throw new IllegalArgumentException(
                    "Actor reply terminal mismatch");
            }
            List<Message> replyParts = header.terminalResult() == 0
                ? List.of(Message.from(
                    wire.decodeApplicationPayload(frames.get(1)).payload()))
                : List.of();
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    header.terminalResult() == 0
                        ? ZLinkBackendRequestResult.OK
                        : backendResult(header.terminalResult()),
                    Optional.of(actor.nodeRid()),
                    Optional.empty(),
                    Optional.of(correlation),
                    replyParts));
        } catch (RuntimeException failure) {
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    ZLinkBackendRequestResult.PROTOCOL_ERROR,
                    Optional.of(actor.nodeRid()),
                    Optional.empty(),
                    Optional.of(correlation),
                    List.of()));
        }
    }

    private boolean request(
        RoutingId target,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        Duration timeout,
        String channelName) {
        java.util.Objects.requireNonNull(callback, "callback");
        long correlation = allocateCorrelation();
        int flags = metadata == null || metadata.length == 0
            ? 0
            : ServiceWireConstants.FLAG_METADATA;
        List<byte[]> frames = new ArrayList<>();
        frames.add(channelName == null
            ? wire.encodeNodeRequestHeader(correlation, flags)
            : wire.encodeChannelRequestHeader(correlation, channelName, flags));
        if (flags != 0) {
            frames.add(metadata.clone());
        }
        frames.add(wire.encodeApplicationPayload(applicationPayload(parts)));
        ZLinkServiceOperationRegistry.Operation<ZLinkBackendReceived> operation =
            operations.register(timeout);
        operation.completion().whenComplete((reply, failure) -> {
            if (failure == null) {
                callback.handle(reply);
                return;
            }
            callback.handle(new ZLinkBackendReceived(
                failure instanceof java.util.concurrent.TimeoutException
                    ? ZLinkBackendRequestResult.TIMED_OUT
                    : ZLinkBackendRequestResult.INTERNAL_ERROR,
                Optional.of(target),
                Optional.empty(),
                Optional.empty(),
                List.of()));
        });
        boolean submitted = port.request(
            requireStarted(),
            target,
            frames,
            timeout,
            (result, replyFrames) -> completeRequest(
                operation.id(),
                target,
                correlation,
                result,
                replyFrames));
        if (!submitted) {
            operations.complete(
                operation.id(),
                new ZLinkBackendReceived(
                    ZLinkBackendRequestResult.NOT_CONNECTED,
                    Optional.of(target),
                    Optional.empty(),
                    Optional.empty(),
                    List.of()));
        }
        return submitted;
    }

    private void completeRequest(
        UUID operationId,
        RoutingId target,
        long correlation,
        systems.zlink.contracts.sockets.RequestResult result,
        List<byte[]> frames) {
        if (result != systems.zlink.contracts.sockets.RequestResult.OK) {
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    backendResult(result),
                    Optional.of(target),
                    Optional.empty(),
                    Optional.empty(),
                    List.of()));
            return;
        }
        try {
            if (frames.isEmpty() || frames.size() > 2) {
                throw new IllegalArgumentException("invalid service reply frame count");
            }
            ZLinkServiceM6AWireCodec.Reply header =
                wire.decodeReplyHeader(frames.getFirst());
            if (header.correlation() != correlation
                || (header.terminalResult() == 0) != (frames.size() == 2)) {
                throw new IllegalArgumentException("service reply terminal mismatch");
            }
            List<Message> parts = header.terminalResult() == 0
                ? List.of(Message.from(
                    wire.decodeApplicationPayload(frames.get(1)).payload()))
                : List.of();
            ZLinkBackendRequestResult terminal = header.terminalResult() == 0
                ? ZLinkBackendRequestResult.OK
                : backendResult(header.terminalResult());
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    terminal,
                    Optional.of(target),
                    Optional.empty(),
                    Optional.empty(),
                    parts));
        } catch (RuntimeException failure) {
            operations.complete(
                operationId,
                new ZLinkBackendReceived(
                    ZLinkBackendRequestResult.PROTOCOL_ERROR,
                    Optional.of(target),
                    Optional.empty(),
                    Optional.empty(),
                    List.of()));
        }
    }

    void admitPeerChannels(
        RoutingId peerRoutingId,
        Map<String, Integer> channels) {
        java.util.Objects.requireNonNull(peerRoutingId, "peerRoutingId");
        Map<String, Integer> validated = new LinkedHashMap<>();
        channels.entrySet().stream()
            .sorted(Map.Entry.comparingByKey())
            .forEach(entry -> {
                String name = requireChannel(entry.getKey());
                int weight = entry.getValue();
                if (weight < 0 || weight > 100) {
                    throw new IllegalArgumentException(
                        "peer channel weight must be in 0..100");
                }
                validated.put(name, weight);
            });
        admittedPeerChannels.put(peerRoutingId, Map.copyOf(validated));
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        state = MeshNodeState.STOPPED;
        ExecutorService currentPump = pump;
        if (currentPump != null) {
            currentPump.shutdownNow();
        }
        ZLinkServiceMailbox currentMailbox = mailbox;
        if (currentMailbox != null) {
            currentMailbox.close();
        }
        dispatchEnvelopes.values().forEach(ZLinkMeshDispatchRecord::close);
        dispatchEnvelopes.clear();
        admittedPeerChannels.clear();
        operations.close();
        deadlines.shutdownNow();
        SocketMonitor currentMonitor = rawMonitor;
        if (currentMonitor != null) {
            currentMonitor.close();
        }
        port.close();
    }

    private void startPump() {
        pump = Executors.newSingleThreadExecutor(Thread.ofVirtual()
            .name("zlink-jvm-raw-mesh-" + meshName)
            .factory());
        pump.execute(() -> {
            while (!closed.get()) {
                long now = System.nanoTime();
                drainMonitorEvents();
                announceExpectedPeers(now);
                tickLiveness(now);
                Optional<ZLinkJavaRawServicePort.Inbound> inbound =
                    port.receive(requireStarted());
                if (inbound.isEmpty()) {
                    java.util.concurrent.locks.LockSupport.parkNanos(
                        Duration.ofMillis(1).toNanos());
                    continue;
                }
                dispatch(inbound.orElseThrow());
            }
        });
    }

    private void dispatch(ZLinkJavaRawServicePort.Inbound inbound) {
        List<byte[]> frames = inbound.frames();
        if (frames.isEmpty() || frames.getFirst().length < PREFIX_BYTES) {
            return;
        }
        byte[] head = frames.getFirst();
        ZLinkServiceM6AWireCodec.Header header;
        try {
            header = wire.decodeHeader(head);
        } catch (RuntimeException invalid) {
            return;
        }
        int command = header.command();
        int flags = header.flags();
        if (command == ServiceWireConstants.COMMAND_HELLO
            || command == ServiceWireConstants.COMMAND_ADMIT
            || command == ServiceWireConstants.COMMAND_UPDATE) {
            dispatchAdmission(inbound, command);
            return;
        }
        if (command == ServiceWireConstants.COMMAND_REJECT) {
            if (frames.size() == 1) {
                try {
                    wire.decodeReject(head);
                    disconnectAdmitted(inbound.source());
                } catch (RuntimeException ignored) {
                }
            }
            return;
        }
        if (command == ServiceWireConstants.COMMAND_LIVENESS_PROBE
            || command == ServiceWireConstants.COMMAND_LIVENESS_ACK) {
            dispatchLiveness(inbound, command);
            return;
        }
        if (topology.peer(inbound.source()).isEmpty()) {
            return;
        }
        if (command == ServiceWireConstants.COMMAND_SPOT_SEND
            || command == ServiceWireConstants.COMMAND_SPOT_REQUEST) {
            dispatchSpot(inbound, flags);
            return;
        }
        if (command == ServiceWireConstants.COMMAND_LOGICAL_MULTICAST) {
            dispatchLogicalMulticast(inbound, flags);
            return;
        }
        if (command == ServiceWireConstants.COMMAND_ACTOR_SEND
            || command == ServiceWireConstants.COMMAND_ACTOR_REQUEST) {
            dispatchActor(inbound, flags);
            return;
        }
        if (command == ServiceWireConstants.COMMAND_INSTANCE_SPOT) {
            dispatchInstanceSpot(inbound, flags);
            return;
        }
        RecordKind kind;
        String channelName = null;
        Long correlation = null;
        if (command == ServiceWireConstants.COMMAND_NODE_SEND) {
            if (head.length != PREFIX_BYTES) {
                return;
            }
            kind = RecordKind.NODE_SEND;
        } else if (command == ServiceWireConstants.COMMAND_NODE_REQUEST) {
            kind = RecordKind.NODE_REQUEST;
            correlation = wire.decodeNodeRequestHeader(head);
        } else if (command == ServiceWireConstants.COMMAND_CHANNEL_SEND) {
            kind = RecordKind.CHANNEL_SEND;
            channelName = wire.decodeChannelSendHeader(head);
        } else if (command == ServiceWireConstants.COMMAND_CHANNEL_REQUEST) {
            kind = RecordKind.CHANNEL_REQUEST;
            ZLinkServiceM6AWireCodec.ChannelRequest request =
                wire.decodeChannelRequestHeader(head);
            correlation = request.correlation();
            channelName = request.channelName();
        } else {
            return;
        }
        int payloadOffset = (flags & ServiceWireConstants.FLAG_METADATA) == 0
            ? 1
            : 2;
        if (frames.size() != payloadOffset + 1
            || (correlation != null && inbound.requestSequence() == null)) {
            return;
        }
        byte[] metadata = payloadOffset == 2
            ? frames.get(1).clone()
            : new byte[0];
        ZLinkServiceM6AWireCodec.ApplicationPayload payload;
        try {
            payload = wire.decodeApplicationPayload(frames.get(payloadOffset));
        } catch (RuntimeException invalid) {
            return;
        }
        List<Message> messages = List.of(
            Message.from(payload.packetName().getBytes(StandardCharsets.UTF_8)),
            Message.from(payload.payload()));
        ReceiveRecord receive = new ReceiveRecord(
            kind,
            ReadyDomain.APPLICATION.value(),
            inbound.source(),
            null,
            null,
            null,
            OperationKind.NONE,
            null,
            channelName,
            null,
            metadata,
            0,
            0,
            0,
            messages.size());
        Long requestCorrelation = correlation;
        AtomicBoolean replied = new AtomicBoolean();
        Consumer<List<Message>> reply = correlation == null
            ? null
            : replyParts -> {
                if (!replied.compareAndSet(false, true)) {
                    throw new IllegalStateException(
                        "service request already has a terminal reply");
                }
                List<byte[]> replyFrames = List.of(
                    wire.encodeReplyHeader(requestCorrelation, 0, 0),
                    wire.encodeApplicationPayload(
                        applicationPayload(replyParts)));
                port.reply(
                    requireStarted(),
                    inbound.source(),
                    inbound.requestSequence(),
                    replyFrames);
            };
        ZLinkMeshDispatchRecord dispatch = new ZLinkMeshDispatchRecord(
            new ReadyRecord(
                OwnerKind.NODE,
                ReadyDomain.APPLICATION.value(),
                null,
                null),
            receive,
            messages,
            reply);
        long envelopeId = nextDispatchEnvelope.getAndIncrement();
        ZLinkServiceMailbox currentMailbox = mailbox;
        String owner = channelName == null
            ? "node:" + routingId
            : "channel:" + channelName;
        if (currentMailbox == null
            || !currentMailbox.tryEnqueue(new ZLinkServiceMailbox.Record(
                owner,
                ZLinkServiceMailbox.Domain.APPLICATION,
                frames,
                inbound.source().toBytes(),
                null,
                envelopeId))) {
            dispatch.close();
            return;
        }
        dispatchEnvelopes.put(envelopeId, dispatch);
        drainApplicationMailbox();
    }

    private void dispatchSpot(
        ZLinkJavaRawServicePort.Inbound inbound,
        int flags) {
        List<byte[]> frames = inbound.frames();
        int payloadOffset = (flags & ServiceWireConstants.FLAG_METADATA) == 0
            ? 1
            : 2;
        if (frames.size() != payloadOffset + 1) {
            return;
        }
        ZLinkServiceM6BWireCodec.SpotMessage header;
        ZLinkServiceM6AWireCodec.ApplicationPayload payload;
        try {
            header = statefulWire.decodeSpotHeader(frames.getFirst());
            payload = wire.decodeApplicationPayload(frames.get(payloadOffset));
        } catch (RuntimeException invalid) {
            return;
        }
        if (header.request() != (inbound.requestSequence() != null)
            || !header.target().targetNodeRid().equals(routingId)
            || localDescriptor == null
            || header.target().targetNodeGeneration()
                != localDescriptor.lifecycleGeneration()) {
            replySpotFailure(inbound, header, 102, 1);
            return;
        }
        byte[] metadata = payloadOffset == 2
            ? frames.get(1).clone()
            : new byte[0];
        List<Message> messages = List.of(
            Message.from(payload.packetName().getBytes(StandardCharsets.UTF_8)),
            Message.from(payload.payload()));
        AtomicBoolean terminal = new AtomicBoolean();
        boolean accepted = ((ZLinkJavaRawSpotNode) spotNode()).enqueueRemoteSpot(
            inbound.source(),
            header,
            metadata,
            messages,
            replyParts -> {
                if (!terminal.compareAndSet(false, true)) {
                    return;
                }
                port.reply(
                    requireStarted(),
                    inbound.source(),
                    inbound.requestSequence(),
                    List.of(
                        wire.encodeReplyHeader(
                            header.correlation(), 0, 0),
                        wire.encodeApplicationPayload(
                            applicationPayload(replyParts))));
            });
        if (!accepted) {
            messages.forEach(Message::close);
            replySpotFailure(inbound, header, 102, 1);
        }
    }

    private void replySpotFailure(
        ZLinkJavaRawServicePort.Inbound inbound,
        ZLinkServiceM6BWireCodec.SpotMessage header,
        int terminalResult,
        int failureCode) {
        if (!header.request() || inbound.requestSequence() == null) {
            return;
        }
        port.reply(
            requireStarted(),
            inbound.source(),
            inbound.requestSequence(),
            List.of(wire.encodeReplyHeader(
                header.correlation(),
                terminalResult,
                failureCode)));
    }

    private void dispatchLogicalMulticast(
        ZLinkJavaRawServicePort.Inbound inbound,
        int flags) {
        List<byte[]> frames = inbound.frames();
        int payloadOffset = (flags & ServiceWireConstants.FLAG_METADATA) == 0
            ? 1
            : 2;
        if (frames.size() != payloadOffset + 1
            || inbound.requestSequence() != null) {
            return;
        }
        try {
            ZLinkServiceM6BWireCodec.LogicalMulticast header =
                statefulWire.decodeLogicalMulticastHeader(frames.getFirst());
            if (!admittedPeerChannels
                    .getOrDefault(inbound.source(), Map.of())
                    .containsKey(header.channelName())) {
                return;
            }
            byte[] metadata = payloadOffset == 2
                ? frames.get(1).clone()
                : new byte[0];
            ZLinkServiceM6AWireCodec.ApplicationPayload payload =
                wire.decodeApplicationPayload(frames.get(payloadOffset));
            List<Message> messages = List.of(
                Message.from(
                    payload.packetName().getBytes(StandardCharsets.UTF_8)),
                Message.from(payload.payload()));
            ((ZLinkJavaRawSpotNode) spotNode()).enqueueLogicalMulticast(
                header.channelName(),
                header.topic(),
                header.sourceSpotRid(),
                inbound.source(),
                metadata,
                messages);
            messages.forEach(Message::close);
        } catch (RuntimeException invalid) {
            return;
        }
    }

    private void dispatchInstanceSpot(
        ZLinkJavaRawServicePort.Inbound inbound,
        int flags) {
        List<byte[]> frames = inbound.frames();
        int payloadOffset = (flags & ServiceWireConstants.FLAG_METADATA) == 0
            ? 1
            : 2;
        if (frames.size() != payloadOffset + 1) {
            return;
        }
        ZLinkServiceM6BWireCodec.InstanceSpotMessage header;
        ZLinkServiceM6AWireCodec.ApplicationPayload payload;
        try {
            header =
                statefulWire.decodeInstanceSpotHeader(frames.getFirst());
            payload = wire.decodeApplicationPayload(frames.get(payloadOffset));
        } catch (RuntimeException invalid) {
            return;
        }
        Optional<ZLinkServiceTopologyRegistry.Peer> source =
            topology == null
                ? Optional.empty()
                : topology.peer(inbound.source());
        if (header.request() != (inbound.requestSequence() != null)
            || !header.sourceNodeRid().equals(inbound.source())
            || source.isEmpty()
            || source.orElseThrow().descriptor().lifecycleGeneration()
                != header.sourceNodeGeneration()
            || !header.route().targetNodeRid().equals(routingId)
            || localDescriptor == null
            || header.route().targetNodeGeneration()
                != localDescriptor.lifecycleGeneration()) {
            replyInstanceFailure(inbound, header, 102, 1);
            return;
        }
        byte[] metadata = payloadOffset == 2
            ? frames.get(1).clone()
            : new byte[0];
        List<Message> messages = List.of(
            Message.from(
                payload.packetName().getBytes(StandardCharsets.UTF_8)),
            Message.from(payload.payload()));
        AtomicBoolean terminal = new AtomicBoolean();
        boolean accepted = ((ZLinkJavaRawSpotNode) spotNode())
            .enqueueRemoteInstanceSpot(
                inbound.source(),
                header,
                metadata,
                messages,
                replyParts -> {
                    if (!terminal.compareAndSet(false, true)) {
                        return;
                    }
                    port.reply(
                        requireStarted(),
                        inbound.source(),
                        inbound.requestSequence(),
                        List.of(
                            wire.encodeReplyHeader(
                                header.replyRouteId(), 0, 0),
                            wire.encodeApplicationPayload(
                                applicationPayload(replyParts))));
                });
        if (!accepted) {
            messages.forEach(Message::close);
            replyInstanceFailure(inbound, header, 102, 1);
        }
    }

    private void replyInstanceFailure(
        ZLinkJavaRawServicePort.Inbound inbound,
        ZLinkServiceM6BWireCodec.InstanceSpotMessage header,
        int terminalResult,
        int failureCode) {
        if (!header.request() || inbound.requestSequence() == null) {
            return;
        }
        port.reply(
            requireStarted(),
            inbound.source(),
            inbound.requestSequence(),
            List.of(wire.encodeReplyHeader(
                header.replyRouteId(),
                terminalResult,
                failureCode)));
    }

    private void dispatchActor(
        ZLinkJavaRawServicePort.Inbound inbound,
        int flags) {
        List<byte[]> frames = inbound.frames();
        int payloadOffset = (flags & ServiceWireConstants.FLAG_METADATA) == 0
            ? 1
            : 2;
        if (frames.size() != payloadOffset + 1) {
            return;
        }
        ZLinkServiceM6BWireCodec.ActorMessage header;
        ZLinkServiceM6AWireCodec.ApplicationPayload payload;
        try {
            header = statefulWire.decodeActorHeader(frames.getFirst());
            payload = wire.decodeApplicationPayload(frames.get(payloadOffset));
        } catch (RuntimeException invalid) {
            return;
        }
        if (header.request() != (inbound.requestSequence() != null)
            || !header.target().actor().nodeRid().equals(routingId)
            || localDescriptor == null
            || header.target().targetNodeGeneration()
                != localDescriptor.lifecycleGeneration()) {
            replyActorFailure(inbound, header, 102, 1);
            return;
        }
        List<Message> messages = List.of(
            Message.from(payload.packetName().getBytes(StandardCharsets.UTF_8)),
            Message.from(payload.payload()));
        AtomicBoolean terminal = new AtomicBoolean();
        boolean accepted = ((ZLinkJavaRawSpotNode) spotNode())
            .enqueueRemoteActor(
                inbound.source(),
                header,
                messages,
                replyParts -> {
                    if (!terminal.compareAndSet(false, true)) {
                        return;
                    }
                    port.reply(
                        requireStarted(),
                        inbound.source(),
                        inbound.requestSequence(),
                        List.of(
                            wire.encodeReplyHeader(
                                header.correlation(), 0, 0),
                            wire.encodeApplicationPayload(
                                applicationPayload(replyParts))));
                });
        if (!accepted) {
            messages.forEach(Message::close);
            replyActorFailure(inbound, header, 102, 1);
        }
    }

    private void replyActorFailure(
        ZLinkJavaRawServicePort.Inbound inbound,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        int terminalResult,
        int failureCode) {
        if (!header.request() || inbound.requestSequence() == null) {
            return;
        }
        port.reply(
            requireStarted(),
            inbound.source(),
            inbound.requestSequence(),
            List.of(wire.encodeReplyHeader(
                header.correlation(),
                terminalResult,
                failureCode)));
    }

    private void dispatchAdmission(
        ZLinkJavaRawServicePort.Inbound inbound,
        int command) {
        if (inbound.frames().size() != 1) {
            return;
        }
        try {
            ZLinkServiceNodeDescriptor descriptor =
                wire.decodeAdmission(
                    inbound.frames().getFirst(),
                    command,
                    inbound.source());
            String connectionId = connectionIds.computeIfAbsent(
                inbound.source(),
                ignored -> UUID.randomUUID().toString());
            ZLinkServiceTopologyRegistry.AdmissionResult admitted =
                topology.admit(descriptor, connectionId);
            if (admitted
                != ZLinkServiceTopologyRegistry.AdmissionResult.ADMITTED) {
                port.send(
                    requireStarted(),
                    inbound.source(),
                    List.of(wire.encodeReject(3)));
                return;
            }
            admitPeerChannels(
                inbound.source(),
                descriptor.channels().stream().collect(
                    java.util.stream.Collectors.toMap(
                        ZLinkServiceNodeDescriptor.Channel::name,
                        ZLinkServiceNodeDescriptor.Channel::weight)));
            liveness.admit(inbound.source(), connectionId, System.nanoTime());
            if (command == ServiceWireConstants.COMMAND_HELLO) {
                port.send(
                    requireStarted(),
                    inbound.source(),
                    List.of(wire.encodeAdmission(
                        ServiceWireConstants.COMMAND_ADMIT,
                        localDescriptor)));
            }
        } catch (RuntimeException invalid) {
            port.send(
                requireStarted(),
                inbound.source(),
                List.of(wire.encodeReject(3)));
        }
    }

    private void dispatchLiveness(
        ZLinkJavaRawServicePort.Inbound inbound,
        int command) {
        try {
            ZLinkServiceWireFrame record =
                new ZLinkServiceWireCodec().decode(inbound.frames());
            long probeId = ByteBuffer.wrap(record.frames().getFirst()).getLong();
            ZLinkServiceTopologyRegistry.Peer peer =
                topology.peer(inbound.source()).orElseThrow();
            if (command == ServiceWireConstants.COMMAND_LIVENESS_PROBE) {
                if (liveness.acknowledgeProbe(
                    inbound.source(),
                    peer.connectionId(),
                    probeId).isPresent()) {
                    port.send(
                        requireStarted(),
                        inbound.source(),
                        encodeLiveness(
                            ServiceWireConstants.COMMAND_LIVENESS_ACK,
                            probeId));
                }
            } else {
                liveness.acknowledge(
                    inbound.source(),
                    peer.connectionId(),
                    probeId,
                    System.nanoTime());
            }
        } catch (RuntimeException ignored) {
        }
    }

    private void drainMonitorEvents() {
        MonitorEvent event;
        while ((event = monitorEvents.poll()) != null) {
            Optional<RoutingId> peer = event.routingId();
            if (peer.isEmpty()) {
                continue;
            }
            if (event.event() == MonitorEventType.CONNECTION_READY) {
                connectionIds.put(
                    peer.orElseThrow(),
                    UUID.randomUUID().toString());
                nextAnnouncementNanos.put(peer.orElseThrow(), 0L);
            } else if (event.event() == MonitorEventType.DISCONNECTED
                || event.event() == MonitorEventType.CLOSED) {
                disconnectAdmitted(peer.orElseThrow());
                nextAnnouncementNanos.put(peer.orElseThrow(), 0L);
            }
        }
    }

    private void announceExpectedPeers(long nowNanos) {
        for (PeerIntent intent : peerIntents.values()) {
            RoutingId expected = intent.expectedRoutingId();
            if (expected == null || topology.peer(expected).isPresent()) {
                continue;
            }
            long next = nextAnnouncementNanos.getOrDefault(expected, 0L);
            if (nowNanos < next) {
                continue;
            }
            port.send(
                requireStarted(),
                expected,
                List.of(wire.encodeAdmission(
                    ServiceWireConstants.COMMAND_HELLO,
                    localDescriptor)));
            nextAnnouncementNanos.put(
                expected,
                nowNanos + Duration.ofMillis(100).toNanos());
        }
    }

    private void tickLiveness(long nowNanos) {
        ZLinkServiceLivenessRegistry.Tick tick = liveness.tick(nowNanos);
        for (ZLinkServiceLivenessRegistry.Probe probe : tick.probes()) {
            port.send(
                requireStarted(),
                probe.nodeRoutingId(),
                encodeLiveness(
                    ServiceWireConstants.COMMAND_LIVENESS_PROBE,
                    probe.probeId()));
        }
        tick.timedOutNodes().forEach(this::disconnectAdmitted);
    }

    private static List<byte[]> encodeLiveness(int command, long probeId) {
        return new ZLinkServiceWireCodec().encode(
            new ZLinkServiceWireFrame(
                command,
                0,
                List.of(ByteBuffer.allocate(Long.BYTES)
                    .putLong(probeId)
                    .array())));
    }

    private void disconnectAdmitted(RoutingId peer) {
        admittedPeerChannels.remove(peer);
        String connectionId = connectionIds.remove(peer);
        if (connectionId != null) {
            topology.disconnect(peer, connectionId);
            liveness.disconnect(peer, connectionId);
        }
    }

    private void drainApplicationMailbox() {
        ZLinkServiceMailbox currentMailbox = mailbox;
        if (currentMailbox == null) {
            return;
        }
        Optional<ZLinkServiceMailbox.Claim> claimed;
        while ((claimed = currentMailbox.tryClaim(
            ZLinkServiceMailbox.Domain.APPLICATION,
            64,
            1024L * 1024)).isPresent()) {
            ZLinkServiceMailbox.Claim claim = claimed.orElseThrow();
            try {
                for (ZLinkServiceMailbox.Record record : claim.records()) {
                    Long envelopeId = record.correlation();
                    ZLinkMeshDispatchRecord dispatch =
                        dispatchEnvelopes.remove(envelopeId);
                    if (dispatch != null) {
                        boolean accepted = false;
                        try {
                            receiver.accept(dispatch);
                            accepted = true;
                        } finally {
                            if (!accepted) {
                                dispatch.close();
                            }
                        }
                    }
                }
            } finally {
                currentMailbox.release(claim);
            }
        }
    }

    private RouterSocket requireStarted() {
        RouterSocket current = router;
        if (current == null || closed.get()) {
            throw new IllegalStateException("raw MeshNode is not started");
        }
        return current;
    }

    private void requireCreated() {
        if (state != MeshNodeState.CREATED) {
            throw new IllegalStateException("MeshNode configuration is closed");
        }
    }

    private static String requireChannel(String value) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("channelName is required");
        }
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 255 || value.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("channelName exceeds text8");
        }
        return value;
    }

    private ZLinkServiceNodeDescriptor descriptor(
        long lifecycle,
        long revision,
        List<ZLinkServiceNodeDescriptor.Channel> channels,
        ZLinkServiceNodeDescriptor.State descriptorState) {
        return new ZLinkServiceNodeDescriptor(
            meshName,
            routingId,
            lifecycle,
            revision,
            bindEndpoint,
            channels,
            descriptorState,
            routingId.toString(),
            4 * 1024 * 1024,
            0,
            List.of(ZLinkServiceNodeDescriptor.REQUIRED_CAPABILITY),
            ZLinkServiceNodeDescriptor.ObjectRole.SERVER,
            100,
            10_000,
            128,
            0,
            0);
    }

    private static long positiveRandomLong() {
        return ThreadLocalRandom.current().nextLong(1, Long.MAX_VALUE);
    }

    private long allocateCorrelation() {
        long value = nextCorrelation.getAndIncrement();
        if (value <= 0) {
            throw new IllegalStateException("service correlation is exhausted");
        }
        return value;
    }

    private static ZLinkServiceM6AWireCodec.ApplicationPayload applicationPayload(
        List<Message> parts) {
        if (parts == null || parts.isEmpty()) {
            throw new IllegalArgumentException("application message is required");
        }
        String packetName = parts.size() > 1
            ? parts.getFirst().toUtf8String()
            : "message";
        byte[] payload = parts.size() > 1
            ? parts.get(1).toByteArray()
            : parts.getFirst().toByteArray();
        return new ZLinkServiceM6AWireCodec.ApplicationPayload(
            packetName,
            "application/zlink-framework-json-v1",
            payload);
    }

    private static ZLinkBackendRequestResult backendResult(
        systems.zlink.contracts.sockets.RequestResult result) {
        return result == systems.zlink.contracts.sockets.RequestResult.BACKPRESSURED
            ? ZLinkBackendRequestResult.BUSY
            : ZLinkBackendRequestResult.valueOf(result.name());
    }

    private static ZLinkBackendRequestResult backendResult(int wireValue) {
        for (ZLinkBackendRequestResult value : ZLinkBackendRequestResult.values()) {
            if (value.ordinal() == 0 && wireValue == 0) {
                return value;
            }
            if (wireValue == 101 + value.ordinal() - 1) {
                return value;
            }
        }
        return ZLinkBackendRequestResult.PROTOCOL_ERROR;
    }

    private record PeerIntent(
        String endpoint,
        RoutingId expectedRoutingId,
        long createdAtMs) {
    }
}
