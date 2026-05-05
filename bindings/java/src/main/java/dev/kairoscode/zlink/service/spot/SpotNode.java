/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RequestException;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.ActorInterop;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.service.discovery.Discovery;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.function.BiConsumer;

/** Lifecycle and topology facade for the current unified spot node model. */
public final class SpotNode implements AutoCloseable {
    private static final int OPT_SNDHWM = 23;
    private static final int OPT_RCVHWM = 24;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_ACTOR_ADMISSION =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private final Object lifecycleLock = new Object();
    private final Set<Spot> liveSpots =
      Collections.newSetFromMap(new IdentityHashMap<>());
    private MemorySegment handle;
    private final SpotNodeSocketOptions socketOptions = new SpotNodeSocketOptions();
    private ActorAdmissionHandler actorAdmissionHandler;
    private Arena actorAdmissionArena;
    private MemorySegment actorAdmissionStub = MemorySegment.NULL;

    /** Creates a spot node owned by the supplied context. */
    public SpotNode(Context ctx) {
        this(ctx, null);
    }

    /** Creates a spot node with an explicit creation mode. */
    public SpotNode(Context ctx, SpotNodeOptions options) {
        Objects.requireNonNull(ctx, "ctx");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeOptions = MemorySegment.NULL;
            if (options != null) {
                nativeOptions =
                  arena.allocate(NativeLayouts.SPOT_NODE_OPTIONS_LAYOUT);
                nativeOptions.set(ValueLayout.JAVA_INT, 0,
                  options.mode().getValue());
            }
            this.handle = Native.spotNodeNew(
              InternalAccess.contextHandle(ctx), nativeOptions);
        }
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_spot_node_new");
    }

    MemorySegment handle() {
        return handle;
    }

    /** Internal bridge for binding helpers. */
    public MemorySegment handleInternal() {
        return handle();
    }

    /** Binds the local spot node endpoint. */
    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeBind(handle, NativeHelpers.toCString(arena,
              endpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_spot_node_bind");
        }
    }

    /** Connects one peer spot node endpoint. */
    public void connectPeer(String peerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeConnectPeer(handle,
              NativeHelpers.toCString(arena, peerEndpoint));
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_connect_peer");
            }
        }
    }

    /** Disconnects one peer spot node endpoint. */
    public void disconnectPeer(String peerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeDisconnectPeer(handle,
              NativeHelpers.toCString(arena, peerEndpoint));
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_disconnect_peer");
            }
        }
    }

    /** Disconnects the peer spot node identified by target node routing id. */
    public void disconnectPeerRid(RoutingId targetNodeRid) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, targetNodeRid);
            int rc = Native.spotNodeDisconnectPeerRid(handle, nativeRid);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_disconnect_peer_rid");
            }
        }
    }

    /** Attaches a fixed-service discovery view to the node. */
    public void attachDiscovery(Discovery discovery) {
        Objects.requireNonNull(discovery, "discovery");
        int rc = Native.spotNodeAttachDiscovery(handle,
            InternalAccess.discoveryHandle(discovery));
        if (rc != 0) {
            throw ZlinkException.fromLastError(
              "zlink_spot_node_attach_discovery");
        }
    }

    /** Attaches one discovery-managed channel DEALER to the node. */
    public void attachChannelDealer(Discovery discovery, DealerSocket dealer) {
        Objects.requireNonNull(discovery, "discovery");
        Objects.requireNonNull(dealer, "dealer");
        int rc = Native.spotNodeAttachChannelDealer(handle,
          InternalAccess.discoveryHandle(discovery),
          InternalAccess.socketHandle(dealer));
        if (rc != 0) {
            throw ZlinkException.fromLastError(
              "zlink_spot_node_attach_channel_dealer");
        }
    }

    /** Attaches one manually connected channel DEALER to the node. */
    public void attachChannelDealerManual(String channelName,
                                          DealerSocket dealer) {
        Objects.requireNonNull(dealer, "dealer");
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeAttachChannelDealerManual(handle,
              NativeHelpers.toCString(arena, requireServiceName(channelName)),
              InternalAccess.socketHandle(dealer));
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_attach_channel_dealer_manual");
            }
        }
    }

    /** Attaches one dedicated publish ingress PUB socket to the node. */
    public void attachPubIngress(PubSocket pub) {
        Objects.requireNonNull(pub, "pub");
        int rc = Native.spotNodeAttachPubIngress(handle,
          InternalAccess.socketHandle(pub));
        if (rc != 0) {
            throw ZlinkException.fromLastError(
              "zlink_spot_node_attach_pub_ingress");
        }
    }

    /** Configures server TLS credentials on the node transport surface. */
    public void setTlsServer(String certPem, String keyPem,
                             boolean requireClientCert) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetTlsServer(handle,
              NativeHelpers.toCString(arena, certPem),
              NativeHelpers.toCString(arena, keyPem));
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_set_tls_server");
            }
        }
    }

    /** Configures client TLS credentials on the node transport surface. */
    public void setTlsClient(String caCertPem, String hostname,
                             boolean trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetTlsClient(handle,
              NativeHelpers.toCString(arena, caCertPem),
              NativeHelpers.toCString(arena, hostname),
              trustSystem ? 1 : 0);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_set_tls_client");
            }
        }
    }

    /** Sets the logical routing id for this spot node. */
    public void setRoutingId(RoutingId rid) {
        Objects.requireNonNull(rid, "rid");
        ensureOpen();
        byte[] value = InternalAccess.routingIdTrustedBytes(rid);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(value.length);
            if (value.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeValue,
                  0, value.length);
            }
            int rc = Native.setRoutingId(handle, nativeValue, value.length);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_set_routing_id");
            }
        }
    }

    /** Returns the current logical routing id for this spot node. */
    public RoutingId routingId() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment outRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            int rc = Native.getRoutingId(handle, outRid);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_get_routing_id");
            }
            int size = outRid.get(ValueLayout.JAVA_BYTE,
              NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
            byte[] value = new byte[size];
            if (size > 0) {
                MemorySegment.copy(outRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
                  MemorySegment.ofArray(value), 0, size);
            }
            return RoutingId.fromBytes(value);
        }
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId rid) {
        byte[] value = rid.toBytes();
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
          (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    /** Creates one spot handle owned by this node. */
    public Spot createSpot() {
        Spot spot = new Spot(this);
        synchronized (lifecycleLock) {
            if (isClosed()) {
                spot.close();
                throw new dev.kairoscode.zlink.ConfigException(
                  dev.kairoscode.zlink.ConfigResult.INVALID_HANDLE);
            }
            liveSpots.add(spot);
        }
        return spot;
    }

    /** Creates one local Actor owned by this node. */
    public Actor actor(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment actor = Native.spotNodeActorNew(handle,
              NativeHelpers.toCString(arena, actorId));
            if (actor == null || actor.address() == 0) {
                throw ZlinkException.fromLastError("zlink_spot_node_actor_new");
            }
            return new Actor(actor);
        }
    }

    /** Looks up a live local Actor ref by id. */
    public ActorRef actorLookup(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = Native.spotNodeActorLookup(handle,
              NativeHelpers.toCString(arena, actorId), out);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_actor_lookup");
            }
            return ActorInterop.actorRefFromNative(out);
        }
    }

    /** Builds an unchecked remote Actor ref for request APIs. */
    public static ActorRef remoteActorRef(RoutingId targetNodeRid,
                                          String actorId) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = Native.remoteActorGetRef(
              ActorInterop.nativeRoutingId(arena, targetNodeRid),
              NativeHelpers.toCString(arena, actorId), out);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_remote_actor_get_ref");
            }
            return ActorInterop.actorRefFromNative(out);
        }
    }

    public ActorCreateResult createRemoteActor(RoutingId targetNodeRid,
                                               String actorId,
                                               Message message,
                                               Duration timeout) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        Objects.requireNonNull(message, "message");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(message, nativeMsg);
            MemorySegment out = arena.allocate(
              NativeLayouts.ACTOR_CREATE_RESULT_LAYOUT);
            int rc = Native.spotNodeCreateRemoteActor(handle,
              ActorInterop.nativeRoutingId(arena, targetNodeRid),
              NativeHelpers.toCString(arena, actorId), nativeMsg, out,
              timeoutMillis(timeout));
            if (rc != 0) {
                NativeMsg.msgClose(nativeMsg);
                throw new RequestException(RequestResult.fromValue(rc));
            }
            return ActorInterop.actorCreateResultFromNative(out);
        }
    }

    public ActorCreateResult createRemoteActor(RoutingId targetNodeRid,
                                               String actorId,
                                               Message message) {
        return createRemoteActor(targetNodeRid, actorId, message,
          Duration.ofMillis(5_000L));
    }

    public void destroyRemoteActor(ActorRef actor, Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeDestroyRemoteActor(handle,
              ActorInterop.actorRefToNative(arena, actor), timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
    }

    public void destroyRemoteActor(ActorRef actor) {
        destroyRemoteActor(actor, Duration.ofMillis(5_000L));
    }

    public void onActorAdmission(ActorAdmissionHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        Arena arena = Arena.ofShared();
        MemorySegment stub;
        try {
            stub = LINKER.upcallStub(MethodHandles.lookup().findVirtual(
              SpotNode.class, "handleActorAdmission",
              MethodType.methodType(int.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class, MemorySegment.class))
              .bindTo(this), FD_ACTOR_ADMISSION, arena);
        } catch (ReflectiveOperationException ex) {
            arena.close();
            throw new IllegalStateException("failed to bind admission callback",
              ex);
        }
        int rc = Native.spotNodeActorAdmissionHandler(handle, stub,
          MemorySegment.NULL);
        if (rc != 0) {
            arena.close();
            throw ZlinkException.fromLastError(
              "zlink_spot_node_actor_admission_handler");
        }
        if (actorAdmissionArena != null) {
            actorAdmissionArena.close();
        }
        actorAdmissionArena = arena;
        actorAdmissionStub = stub;
        actorAdmissionHandler = handler;
    }

    public boolean joinActor(ActorRef actor,
                             RoutingId destSpotRid,
                             Message message,
                             BiConsumer<RequestResult, List<Message>> callback,
                             Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(callback, "callback");
        ensureOpen();
        ActorRequestCallbacks.PendingToken pending =
          ActorRequestCallbacks.register(callback);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(message, nativeMsg);
            int rc = Native.spotNodeActorJoinSpot(handle,
              ActorInterop.actorRefToNative(arena, actor),
              ActorInterop.nativeRoutingId(arena, destSpotRid),
              nativeMsg, ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(pending.id()), SendFlags.NONE.value(),
              timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(pending.id());
                NativeMsg.msgClose(nativeMsg);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            return true;
        }
    }

    public boolean joinActor(ActorRef actor,
                             RoutingId destSpotRid,
                             Message message,
                             BiConsumer<RequestResult, List<Message>> callback) {
        return joinActor(actor, destSpotRid, message, callback,
          Duration.ofMillis(5_000L));
    }

    public void leaveActor(ActorRef actor, RoutingId destSpotRid,
                           Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorLeaveSpot(handle,
              ActorInterop.actorRefToNative(arena, actor),
              ActorInterop.nativeRoutingId(arena, destSpotRid),
              timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
    }

    public void leaveActor(ActorRef actor, RoutingId destSpotRid) {
        leaveActor(actor, destSpotRid, Duration.ofMillis(5_000L));
    }

    void sendHwm(int value) {
        socketOptions.setPubIntOption(OPT_SNDHWM, value);
    }

    void recvHwm(int value) {
        socketOptions.setSubIntOption(OPT_RCVHWM, value);
    }

    /** Returns the current node status snapshot. */
    public SpotNodeStatus statusSnapshot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.SPOT_NODE_STATUS_LAYOUT);
            int rc = Native.spotNodeStatusSnapshot(handle, out);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_status_snapshot");
            }
            return SpotNodeStatus.fromNative(out);
        }
    }

    /** Returns the current peer snapshot. */
    public List<SpotNodePeerEntry> peersSnapshot() {
        return readPeerEntries(null);
    }

    /** Returns peer entries matching the supplied filter. */
    public List<SpotNodePeerEntry> peersQuery(SpotNodePeerFilter filter) {
        Objects.requireNonNull(filter, "filter");
        return readPeerEntries(filter);
    }

    /** Returns the current subject snapshot. */
    public List<SpotNodeSubjectEntry> subjectsSnapshot() {
        return subjectsSnapshot(null);
    }

    /** Returns subject entries matching the supplied filter. */
    public List<SpotNodeSubjectEntry> subjectsSnapshot(
      SpotNodeSubjectFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : filter.toNative(arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeSubjectsSnapshot(handle, nativeFilter,
              MemorySegment.NULL, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_subjects_snapshot");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeSubjectsSnapshot(handle, nativeFilter, entries,
              count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_subjects_snapshot");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride =
              NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodeSubjectEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(SpotNodeSubjectEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns internal socket rows that actually exist on this node. */
    public List<SpotNodeSocketSnapshotEntry> internalSocketsSnapshot() {
        return internalSocketsSnapshot(null);
    }

    public List<SpotNodeSpotEntry> spotsSnapshot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeSpotsSnapshot(handle, MemorySegment.NULL,
              count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_spots_snapshot");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeSpotsSnapshot(handle, entries, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_spots_snapshot");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodeSpotEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(ActorInterop.spotEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    public List<SpotNodeActorEntry> actorsSnapshot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeActorsSnapshot(handle, MemorySegment.NULL,
              count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_actors_snapshot");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeActorsSnapshot(handle, entries, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_actors_snapshot");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodeActorEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(ActorInterop.actorEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns internal socket rows matching the supplied filter. */
    public List<SpotNodeSocketSnapshotEntry> internalSocketsSnapshot(
      SpotNodeSocketSnapshotFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : filter.toNative(arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeInternalSocketsSnapshot(handle,
              nativeFilter, MemorySegment.NULL, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_internal_sockets_snapshot");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeInternalSocketsSnapshot(handle, nativeFilter,
              entries, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_spot_node_internal_sockets_snapshot");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride =
              NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodeSocketSnapshotEntry> out =
              new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(SpotNodeSocketSnapshotEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    @Override
    public void close() {
        List<Spot> ownedSpots;
        MemorySegment nodeHandle;
        synchronized (lifecycleLock) {
            if (isClosed())
                return;
            ownedSpots = List.copyOf(liveSpots);
            liveSpots.clear();
            nodeHandle = handle;
            handle = MemorySegment.NULL;
        }
        RuntimeException closeFailure = null;
        for (Spot spot : ownedSpots) {
            try {
                spot.close();
            } catch (RuntimeException ex) {
                if (closeFailure == null) {
                    closeFailure = ex;
                }
            }
        }
        Native.spotNodeDestroy(nodeHandle);
        if (actorAdmissionArena != null) {
            actorAdmissionArena.close();
            actorAdmissionArena = null;
            actorAdmissionStub = MemorySegment.NULL;
            actorAdmissionHandler = null;
        }
        if (closeFailure != null) {
            throw closeFailure;
        }
    }

    void releaseSpot(Spot spot) {
        synchronized (lifecycleLock) {
            liveSpots.remove(spot);
        }
    }

    private boolean isClosed() {
        return handle == null || handle.address() == 0;
    }

    private void ensureOpen() {
        if (isClosed()) {
            throw new IllegalStateException("spot node is closed");
        }
    }

    private List<SpotNodePeerEntry> readPeerEntries(SpotNodePeerFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : filter.toNative(arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = filter == null
              ? Native.spotNodePeersSnapshot(handle, MemorySegment.NULL, count)
              : Native.spotNodePeersQuery(handle, nativeFilter,
                MemorySegment.NULL, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(filter == null
                  ? "zlink_spot_node_peers_snapshot"
                  : "zlink_spot_node_peers_query");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_PEER_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = filter == null
              ? Native.spotNodePeersSnapshot(handle, entries, count)
              : Native.spotNodePeersQuery(handle, nativeFilter, entries, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(filter == null
                  ? "zlink_spot_node_peers_snapshot"
                  : "zlink_spot_node_peers_query");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.SPOT_NODE_PEER_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodePeerEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(SpotNodePeerEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }

    private static String requireServiceName(String serviceName) {
        Objects.requireNonNull(serviceName, "serviceName");
        if (serviceName.isEmpty()) {
            throw new IllegalArgumentException("serviceName must not be empty");
        }
        return serviceName;
    }

    @SuppressWarnings("unused")
    private int handleActorAdmission(MemorySegment node,
                                     MemorySegment actorId,
                                     MemorySegment message,
                                     MemorySegment userdata) {
        ActorAdmissionHandler handler = actorAdmissionHandler;
        if (handler == null) {
            return ActorAdmissionResult.REJECT.value();
        }
        try {
            int size = Math.toIntExact(NativeMsg.msgSize(message));
            byte[] bytes = new byte[size];
            if (size > 0) {
                MemorySegment.copy(NativeMsg.msgData(message).reinterpret(size),
                  0, MemorySegment.ofArray(bytes), 0, size);
            }
            try (Message copied = Message.copyOf(bytes)) {
                return handler.onActorAdmission(
                  ActorInterop.readCString(actorId, NativeLayouts.ACTOR_ID_MAX),
                  copied).value();
            }
        } catch (RuntimeException ex) {
            return ActorAdmissionResult.REJECT.value();
        }
    }

    private static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }

    private final class SpotNodeSocketOptions {
        void setPubIntOption(int optionId, int value) {
            setIntOption(optionId, value, true);
        }

        void setSubIntOption(int optionId, int value) {
            setIntOption(optionId, value, false);
        }

        private void setIntOption(int optionId,
                                  int value,
                                  boolean publishOption)
        {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
                nativeValue.set(ValueLayout.JAVA_INT, 0, value);
                int rc = publishOption
                  ? Native.setPubOption(handle, optionId, nativeValue,
                      Integer.BYTES)
                  : Native.setSubOption(handle, optionId, nativeValue,
                      Integer.BYTES);
                if (rc != 0) {
                    throw ZlinkException.fromLastError(publishOption
                      ? "zlink_set_pub_option"
                      : "zlink_set_sub_option");
                }
            }
        }
    }
}
