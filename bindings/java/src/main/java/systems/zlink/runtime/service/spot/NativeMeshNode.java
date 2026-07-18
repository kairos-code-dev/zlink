/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ErrorCategory;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorLocation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.DrainResult;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.MeshNodePublisher;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.service.spot.MeshReadyHandler;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.StreamSessionService;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeRoutingIds;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import systems.zlink.contracts.sockets.RequestResult;
import java.lang.foreign.Arena;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public final class NativeMeshNode implements MeshNode {
    private static final Duration DEFAULT_ACTOR_TIMEOUT = Duration.ofSeconds(5);
    private static final Duration DEFAULT_SHUTDOWN_TIMEOUT = Duration.ofSeconds(30);

    private static final Linker LINKER = Linker.nativeLinker();
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment READY_STUB = LINKER.upcallStub(
        readyHandle(), NativeServiceSymbols.READY_HANDLER_DESCRIPTOR,
        CALLBACK_ARENA);
    private static final ConcurrentHashMap<Long, NativeMeshNode> NODES =
        new ConcurrentHashMap<>();

    private final Context context;
    private MemorySegment handle;
    private volatile MeshReadyHandler readyHandler;
    private final Set<NativeSpot> ownedSpots = ConcurrentHashMap.newKeySet();

    static {
        InternalAccess.register(new InternalAccess.MeshNodeAccess() {
            @Override
            public MemorySegment handle(MeshNode node) {
                return ((NativeMeshNode) node).handle;
            }

            @Override
            public StreamSessionService createStreamSessionService(MeshNode node,
                                                                   StreamSocket stream) {
                return NativeStreamSessionService.create((NativeMeshNode) node, stream);
            }
        });
        InternalAccess.register(new InternalAccess.DispatchAccess() {
            @Override
            public ReadyBatch newReadyBatch(int recordCapacity) {
                return new NativeReadyBatch(recordCapacity);
            }

            @Override
            public ReceiveBatch newReceiveBatch(int messageCapacity, int partCapacity,
                                                int byteCapacity) {
                return new NativeReceiveBatch(messageCapacity, partCapacity, byteCapacity);
            }

            @Override
            public void reply(byte[] token, List<Message> parts, int flags) {
                doReply(token, parts, flags);
            }

            @Override
            public void actorJoinReply(byte[] token, int decision, List<Message> parts,
                                       int flags) {
                doActorJoinReply(token, decision, parts, flags);
            }
        });
    }

    private NativeMeshNode(Context context, MemorySegment handle) {
        this.context = context;
        this.handle = handle;
        NODES.put(handle.address(), this);
    }

    public static NativeMeshNode create(Context context) {
        return create(context, MeshNodeOptions.defaults());
    }

    public static NativeMeshNode create(Context context, MeshNodeOptions options) {
        Objects.requireNonNull(context, "context");
        MeshNodeOptions opts = options == null ? MeshNodeOptions.defaults() : options;
        MemorySegment handle;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment native_ = ServiceInterop.allocStamped(arena,
                ServiceLayouts.MESH_NODE_OPTIONS);
            writeString(arena, native_, ServiceLayouts.MESH_NODE_OPTIONS, "mesh_name",
                "mesh_name_size", opts.meshName());
            writeString(arena, native_, ServiceLayouts.MESH_NODE_OPTIONS, "trust_profile",
                "trust_profile_size", opts.trustProfile());
            handle = NativeServiceSymbols.meshNodeNew(
                InternalAccess.contextHandle(context), native_);
        }
        if (handle == null || handle.address() == 0) {
            throw ZlinkException.fromLastError(ErrorCategory.CONFIG);
        }
        return new NativeMeshNode(context, handle);
    }

    MemorySegment handle() {
        return handle;
    }

    Context context() {
        return context;
    }

    // --- lifecycle / configuration ---
    @Override
    public void setBind(String endpoint) {
        Objects.requireNonNull(endpoint, "endpoint");
        try (Arena arena = Arena.ofConfined()) {
            MeshCalls.configOk(NativeServiceSymbols
                .meshNodeSetBind(handle, NativeHelpers.toCString(arena, endpoint)));
        }
    }

    @Override
    public void addChannel(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        try (Arena arena = Arena.ofConfined()) {
            MeshCalls.configOk(NativeServiceSymbols
                .meshNodeAddChannelName(handle, NativeHelpers.toCString(arena, channelName)));
        }
    }

    @Override
    public void setChannelWeight(String channelName, int weight) {
        Objects.requireNonNull(channelName, "channelName");
        try (Arena arena = Arena.ofConfined()) {
            MeshCalls.configOk(NativeServiceSymbols
                .meshNodeSetChannelWeight(handle, NativeHelpers.toCString(arena, channelName),
                    weight));
        }
    }

    @Override
    public void start() {
        MeshCalls.configOk(NativeServiceSymbols
            .meshNodeStart(handle));
    }

    @Override
    public void shutdown(Duration timeout) {
        MeshCalls.requestOk(NativeServiceSymbols
            .meshNodeShutdown(handle, MeshCalls.timeout(timeout)));
    }

    @Override
    public void shutdown() {
        shutdown(DEFAULT_SHUTDOWN_TIMEOUT);
    }

    @Override
    public MeshNodeStatus status() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = ServiceInterop.allocStamped(arena,
                ServiceLayouts.MESH_NODE_STATUS);
            MeshCalls.configOk(NativeServiceSymbols
                .meshNodeStatus(handle, out));
            return ServiceInterop.meshNodeStatusFromNative(out);
        }
    }

    @Override
    public List<MeshPeerEntry> peers() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment countSeg = arena.allocate(ValueLayout.JAVA_LONG);
            countSeg.set(ValueLayout.JAVA_LONG, 0, 0L);
            int rc = NativeServiceSymbols.meshNodePeers(
                handle, MemorySegment.NULL, countSeg);
            long capacity = countSeg.get(ValueLayout.JAVA_LONG, 0);
            if (rc != MeshCalls.CONFIG_OK && capacity == 0) {
                MeshCalls.configOk(rc);
            }
            if (capacity == 0) {
                return List.of();
            }
            long stride = ServiceLayouts.MESH_PEER_ENTRY.byteSize();
            MemorySegment entries = arena.allocate(stride * capacity);
            for (long i = 0; i < capacity; i++) {
                ServiceInterop.stampHeader(entries.asSlice(i * stride, stride),
                    ServiceLayouts.MESH_PEER_ENTRY);
            }
            countSeg.set(ValueLayout.JAVA_LONG, 0, capacity);
            MeshCalls.configOk(NativeServiceSymbols
                .meshNodePeers(handle, entries, countSeg));
            long n = countSeg.get(ValueLayout.JAVA_LONG, 0);
            List<MeshPeerEntry> out = new ArrayList<>((int) n);
            for (long i = 0; i < n; i++) {
                out.add(ServiceInterop.meshPeerEntryFromNative(
                    entries.asSlice(i * stride, stride)));
            }
            return out;
        }
    }

    @Override
    public long connectPeer(String endpoint) {
        return connectPeerInternal(endpoint, null);
    }

    @Override
    public long connectPeer(String endpoint, RoutingId expectedRid) {
        return connectPeerInternal(endpoint, Objects.requireNonNull(expectedRid, "expectedRid"));
    }

    private long connectPeerInternal(String endpoint, RoutingId expectedRid) {
        Objects.requireNonNull(endpoint, "endpoint");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment opts = ServiceInterop.allocStamped(arena,
                ServiceLayouts.MESH_PEER_CONNECTION_OPTIONS);
            writeString(arena, opts, ServiceLayouts.MESH_PEER_CONNECTION_OPTIONS, "endpoint",
                "endpoint_size", endpoint);
            long hasRidOff = ServiceLayouts.off(ServiceLayouts.MESH_PEER_CONNECTION_OPTIONS,
                "has_expected_rid");
            opts.set(ValueLayout.JAVA_INT_UNALIGNED, hasRidOff, expectedRid != null ? 1 : 0);
            if (expectedRid != null) {
                long ridOff = ServiceLayouts.off(ServiceLayouts.MESH_PEER_CONNECTION_OPTIONS,
                    "expected_rid");
                NativeRoutingIds.write(opts.asSlice(ridOff,
                    NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), expectedRid);
            }
            MemorySegment intentOut = arena.allocate(ValueLayout.JAVA_LONG);
            MeshCalls.connectOk(NativeServiceSymbols
                .meshNodeConnectPeer(handle, opts, intentOut));
            return intentOut.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    @Override
    public void removePeerConnection(long connectionIntentId) {
        MeshCalls.connectOk(NativeServiceSymbols
            .meshNodeRemovePeerConnection(handle, connectionIntentId));
    }

    @Override
    public void disconnectPeer(RoutingId peerRid, long lifecycleGeneration) {
        Objects.requireNonNull(peerRid, "peerRid");
        try (Arena arena = Arena.ofConfined()) {
            MeshCalls.connectOk(NativeServiceSymbols
                .meshNodeDisconnectPeer(handle, ServiceInterop.routingIdToNative(arena, peerRid),
                    lifecycleGeneration));
        }
    }

    // --- messaging ---
    @Override
    public void sendToNode(RoutingId targetRid, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(targetRid, "targetRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, targetRid);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.meshNodeSendToNode(
                handle, rid, MemorySegment.NULL, array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_send_to_node");
        }
    }

    @Override
    public OperationId requestToNode(RoutingId targetRid, List<Message> parts, SendFlags flags,
                                     Duration timeout) {
        Objects.requireNonNull(targetRid, "targetRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, targetRid);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.meshNodeRequestToNode(
                handle, rid, MemorySegment.NULL, array, n, opid, flags.value(),
                MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_request_to_node");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public void sendToChannel(String channelName, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(channelName, "channelName");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.meshNodeSendToChannel(
                handle, name, MemorySegment.NULL, array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_send_to_channel");
        }
    }

    @Override
    public OperationId requestToChannel(String channelName, List<Message> parts, SendFlags flags,
                                        Duration timeout) {
        Objects.requireNonNull(channelName, "channelName");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.meshNodeRequestToChannel(
                handle, name, MemorySegment.NULL, array, n, opid, flags.value(),
                MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_request_to_channel");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public void sendToActor(ActorRef actor, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.sendToActor(
                handle, ref, MemorySegment.NULL, array, n, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_send_to_actor");
        }
    }

    @Override
    public OperationId requestToActor(ActorRef actor, List<Message> parts, SendFlags flags,
                                      Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.requestToActor(
                handle, ref, MemorySegment.NULL, array, n, opid, flags.value(),
                MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_request_to_actor");
            return MeshCalls.operationId(opid);
        }
    }

    // --- actors ---
    @Override
    public Actor createActor(String actorId) {
        return createActor(actorId, List.of());
    }

    @Override
    public Actor createActor(String actorId, List<Message> creationParts) {
        Objects.requireNonNull(actorId, "actorId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment id = NativeHelpers.toCString(arena, actorId);
            MemorySegment array = MeshCalls.parts(arena, creationParts);
            long n = MeshCalls.count(creationParts);
            MemorySegment refOut = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = NativeServiceSymbols.actorNew(
                handle, id, array, n, refOut, 0, MeshCalls.timeout(DEFAULT_ACTOR_TIMEOUT));
            if (rc != RequestResult.OK.value()) {
                MessagePartsBuffer.closeNativeArray(array, (int) n);
                throw new ZlinkRequestException(RequestResult.fromValue(rc), Native.errno());
            }
            ActorRef ref = ServiceInterop.actorRefFromNative(refOut);
            return new NativeActor(this, ref);
        }
    }

    @Override
    public Optional<ActorLocation> actorLookup(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment id = NativeHelpers.toCString(arena, actorId);
            MemorySegment out = ServiceInterop.allocStamped(arena, ServiceLayouts.ACTOR_LOCATION);
            int rc = NativeServiceSymbols.actorLookup(handle, id, out);
            if (rc == MeshCalls.CONFIG_NOT_FOUND) {
                return Optional.empty();
            }
            MeshCalls.configOk(rc);
            return Optional.of(ServiceInterop.actorLocationFromNative(out));
        }
    }

    @Override
    public OperationId actorLookupRemote(RoutingId targetNodeRid, String actorId, Duration timeout) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, targetNodeRid);
            MemorySegment id = NativeHelpers.toCString(arena, actorId);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.actorLookupRemote(
                handle, rid, id, opid, MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, MemorySegment.NULL, 0, "zlink_mesh_node_actor_lookup_remote");
            return MeshCalls.operationId(opid);
        }
    }

    @Override
    public OperationId destroyActor(ActorRef actor, Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment ref = ServiceInterop.actorRefToNative(arena, actor);
            MemorySegment opid = MeshCalls.newOperationId(arena);
            int rc = NativeServiceSymbols.actorDestroy(
                handle, ref, opid, MeshCalls.timeout(timeout));
            MeshCalls.submitOk(rc, MemorySegment.NULL, 0, "zlink_mesh_node_actor_destroy");
            return MeshCalls.operationId(opid);
        }
    }

    // --- spots ---
    @Override
    public Spot createSpot() {
        MemorySegment spotHandle = NativeServiceSymbols.spotNew(handle);
        if (spotHandle == null || spotHandle.address() == 0) {
            throw ZlinkException.fromLastError(ErrorCategory.CONFIG);
        }
        NativeSpot spot = new NativeSpot(this, spotHandle, true);
        ownedSpots.add(spot);
        return spot;
    }

    @Override
    public Spot entrySpot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            MeshCalls.configOk(NativeServiceSymbols
                .nodeEntrySpot(handle, out));
            return new NativeSpot(this, out.get(ValueLayout.ADDRESS, 0), false);
        }
    }

    @Override
    public SpotGetOrCreateResult getOrCreateSpot(RoutingId spotRid) {
        Objects.requireNonNull(spotRid, "spotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, spotRid);
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment created = arena.allocate(ValueLayout.JAVA_INT);
            MeshCalls.configOk(NativeServiceSymbols
                .nodeSpotGetOrNew(handle, rid, out, created));
            NativeSpot spot = new NativeSpot(this, out.get(ValueLayout.ADDRESS, 0), false);
            return new SpotGetOrCreateResult(spot, created.get(ValueLayout.JAVA_INT, 0) != 0);
        }
    }

    @Override
    public Optional<Spot> spotLookup(RoutingId spotRid) {
        Objects.requireNonNull(spotRid, "spotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = ServiceInterop.routingIdToNative(arena, spotRid);
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            int rc = NativeServiceSymbols.nodeSpotLookup(
                handle, rid, out);
            if (rc == MeshCalls.CONFIG_NOT_FOUND) {
                return Optional.empty();
            }
            MeshCalls.configOk(rc);
            return Optional.of(new NativeSpot(this, out.get(ValueLayout.ADDRESS, 0), false));
        }
    }

    @Override
    public MeshNodePublisher createPublisher() {
        MemorySegment pub = NativeServiceSymbols.publisherNew(handle);
        if (pub == null || pub.address() == 0) {
            throw ZlinkException.fromLastError(ErrorCategory.CONFIG);
        }
        return new NativeMeshNodePublisher(pub);
    }

    // --- dispatch ---
    @Override
    public void setReadyHandler(MeshReadyHandler handler) {
        this.readyHandler = handler;
        MemorySegment fn = handler == null ? MemorySegment.NULL : READY_STUB;
        MeshCalls.configOk(NativeServiceSymbols
            .setReadyHandler(handle, fn, MemorySegment.NULL));
    }

    @Override
    public DrainResult drainReady(int domains, ReadyBatch batch, RecvFlags flags) {
        Objects.requireNonNull(batch, "batch");
        NativeReadyBatch nativeBatch = (NativeReadyBatch) batch;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment residue = arena.allocate(ValueLayout.JAVA_INT);
            int rc = NativeServiceSymbols.drainReady(
                handle, domains, nativeBatch.handle(), residue, flags.value());
            return new DrainResult(rc, residue.get(ValueLayout.JAVA_INT, 0) != 0);
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0) {
            return;
        }
        for (NativeSpot spot : ownedSpots) {
            spot.destroyOwned();
        }
        ownedSpots.clear();
        NODES.remove(handle.address(), this);
        NativeServiceSymbols.meshNodeDestroy(handle);
        handle = MemorySegment.NULL;
    }

    void releaseSpot(NativeSpot spot) {
        ownedSpots.remove(spot);
    }

    // --- reply helpers ---
    private static void doReply(byte[] token, List<Message> parts, int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment tok = arena.allocate(ServiceLayouts.REPLY_TOKEN);
            ServiceInterop.writeReplyToken(tok, token);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.meshReply(
                tok, array, n, flags);
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_reply");
        }
    }

    private static void doActorJoinReply(byte[] token, int decision, List<Message> parts,
                                         int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment tok = arena.allocate(ServiceLayouts.REPLY_TOKEN);
            ServiceInterop.writeReplyToken(tok, token);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.actorJoinReply(
                tok, decision, array, n, flags);
            MeshCalls.submitOk(rc, array, n, "zlink_actor_join_reply");
        }
    }

    // --- ready handler upcall ---
    private static MethodHandle readyHandle() {
        try {
            return MethodHandles.lookup().findStatic(NativeMeshNode.class, "handleReady",
                MethodType.methodType(int.class, MemorySegment.class, int.class,
                    MemorySegment.class));
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private static int handleReady(MemorySegment nodeHandle, int readyDomains,
                                   MemorySegment userdata) {
        NativeMeshNode node = NODES.get(nodeHandle.address());
        if (node == null) {
            return readyDomains;
        }
        MeshReadyHandler handler = node.readyHandler;
        if (handler == null) {
            return readyDomains;
        }
        InternalAccess.enterCallback();
        try {
            return handler.onReady(readyDomains);
        } finally {
            InternalAccess.leaveCallback();
        }
    }

    private static void writeString(Arena arena, MemorySegment struct, java.lang.foreign.MemoryLayout layout,
                                    String ptrField, String sizeField, String value) {
        long ptrOff = ServiceLayouts.off(layout, ptrField);
        long sizeOff = ServiceLayouts.off(layout, sizeField);
        if (value == null || value.isEmpty()) {
            struct.set(ValueLayout.ADDRESS, ptrOff, MemorySegment.NULL);
            struct.set(ValueLayout.JAVA_LONG_UNALIGNED, sizeOff, 0L);
            return;
        }
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        MemorySegment cstr = NativeHelpers.toCString(arena, value);
        struct.set(ValueLayout.ADDRESS, ptrOff, cstr);
        struct.set(ValueLayout.JAVA_LONG_UNALIGNED, sizeOff, bytes.length);
    }
}
