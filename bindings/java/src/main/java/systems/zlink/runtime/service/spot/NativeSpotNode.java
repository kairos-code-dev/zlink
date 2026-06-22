/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorDestroyOperation;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotOperation;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ActorLookupOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeActorEntry;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.contracts.service.spot.SpotNodeOptions;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodePeerFilter;
import systems.zlink.contracts.service.spot.SpotNodeSocketEntry;
import systems.zlink.contracts.service.spot.SpotNodeSocketFilter;
import systems.zlink.contracts.service.spot.SpotNodePublisher;
import systems.zlink.contracts.service.spot.SpotNodeSpotEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.service.spot.SpotNodeSubjectFilter;
import systems.zlink.contracts.service.spot.SpotRouteBridge;
import systems.zlink.runtime.nativeapi.DurationConversions;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMonitorStatuses;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;

/** Native-backed lifecycle and topology facade for the current unified spot node model. */
public final class NativeSpotNode implements SpotNode {
    private final Object lifecycleLock = new Object();
    private final Set<Spot> liveSpots =
      Collections.newSetFromMap(new IdentityHashMap<>());
    private final Map<Long, Spot> liveSpotsByHandle = new LinkedHashMap<>();
    private final Context context;
    private MemorySegment handle;
    private final SpotNodeOptionsSupport optionsSupport =
      new SpotNodeOptionsSupport(this);
    private final SpotNodeActorOperations actorOperations =
      new SpotNodeActorOperations(this);

    static {
        InternalAccess.register(new InternalAccess.SpotNodeAccess() {
            @Override
            public MemorySegment handle(SpotNode node) {
                return ((NativeSpotNode) node).handle();
            }

            @Override
            public void releaseSpot(SpotNode node, Spot spot) {
                ((NativeSpotNode) node).releaseSpot(spot);
            }
        });
    }

    public static SpotNode create(Context ctx) {
        return new NativeSpotNode(ctx);
    }

    public static SpotNode create(Context ctx, SpotNodeMode mode) {
        return new NativeSpotNode(ctx, mode);
    }

    public static SpotNode create(Context ctx, SpotNodeOptions options) {
        return new NativeSpotNode(ctx, options);
    }

    /** Creates a spot node owned by the supplied context. */
    NativeSpotNode(Context ctx) {
        this(ctx, SpotNodeMode.ALL);
    }

    /** Creates a spot node with an explicit creation mode. */
    NativeSpotNode(Context ctx, SpotNodeMode mode) {
        this(ctx, new SpotNodeOptions(mode));
    }

    /** Creates a spot node with an explicit creation mode. */
    NativeSpotNode(Context ctx, SpotNodeOptions options) {
        Objects.requireNonNull(ctx, "ctx");
        this.context = ctx;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeOptions = MemorySegment.NULL;
            if (options != null) {
                nativeOptions =
                  arena.allocate(NativeLayouts.SPOT_NODE_OPTIONS_LAYOUT);
                nativeOptions.set(ValueLayout.JAVA_INT, 0,
                  EnumCodecs.spotNodeModeValue(options.mode()));
            }
            this.handle = Native.spotNodeNew(
              InternalAccess.contextHandle(ctx), nativeOptions);
        }
        if (handle == null || handle.address() == 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_node_new");
    }

    MemorySegment handle() {
        return handle;
    }

    /** Binds the local spot node PUB/SUB endpoint. */
    public void setPubBind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetPubBind(handle, NativeHelpers.toCString(arena,
              endpoint));
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_node_set_pub_bind");
        }
    }

    /** Sets the routed ingress endpoint before binding the mesh endpoint. */
    public void setRouterBind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetRouterBind(
              handle, NativeHelpers.toCString(arena, endpoint));
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_set_router_bind");
        }
    }

    /** Connects one peer spot node endpoint. */
    public void connectPeer(String peerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeConnectPeer(handle,
              NativeHelpers.toCString(arena, peerEndpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_connect_peer");
            }
        }
    }

    /** Connects one peer spot node endpoint and associates it with a target node routing id. */
    public void connectPeerRid(RoutingId targetNodeRid, String peerEndpoint) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, targetNodeRid);
            int rc = Native.spotNodeConnectPeerRid(handle, nativeRid,
              NativeHelpers.toCString(arena, peerEndpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_connect_peer_rid");
            }
        }
    }

    /** Disconnects one peer spot node endpoint. */
    public void disconnectPeer(String peerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeDisconnectPeer(handle,
              NativeHelpers.toCString(arena, peerEndpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
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
                throw InternalAccess.zlinkExceptionFromLastError(
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
            throw InternalAccess.zlinkExceptionFromLastError(
              "zlink_spot_node_attach_discovery");
        }
    }

    @Override
    public SpotRouteBridge createRouteBridge() {
        return new NativeSpotRouteBridge(context, this);
    }

    @Override
    public SpotNodePublisher createPublisher() {
        return new NativeSpotNodePublisher(this);
    }

    /** Configures server TLS credentials on the node transport surface. */
    public void setTlsServer(String certPem, String keyPem,
                             boolean requireClientCert) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeSetTlsServer(handle,
              NativeHelpers.toCString(arena, certPem),
              NativeHelpers.toCString(arena, keyPem));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
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
                throw InternalAccess.zlinkExceptionFromLastError(
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
                throw InternalAccess.zlinkExceptionFromLastError("zlink_set_routing_id");
            }
        }
    }

    /** Returns the current logical routing id for this spot node. */
    public RoutingId getRoutingId() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment outRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            int rc = Native.getRoutingId(handle, outRid);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_get_routing_id");
            }
            int size = outRid.get(ValueLayout.JAVA_BYTE,
              NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
            byte[] value = new byte[size];
            if (size > 0) {
                MemorySegment.copy(outRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
                  MemorySegment.ofArray(value), 0, size);
            }
            return RoutingId.from(value);
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
        Spot spot = InternalAccess.spotCreateOwned(this);
        synchronized (lifecycleLock) {
            if (isClosed()) {
                spot.close();
                throw new ZlinkConfigException(ConfigResult.INVALID_HANDLE);
            }
            registerSpot(spot);
        }
        return spot;
    }

    /** Returns a facade for the node entry spot. */
    public Spot entrySpot() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            int rc = Native.spotNodeEntrySpot(handle, out);
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            return adoptSpot(out.get(ValueLayout.ADDRESS, 0));
        }
    }

    /** Looks up a local spot by routing id and returns a facade when present. */
    public Optional<Spot> spotLookup(RoutingId spotRid) {
        Objects.requireNonNull(spotRid, "spotRid");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            int rc = Native.spotNodeSpotLookup(handle,
              ActorInterop.nativeRoutingId(arena, spotRid), out);
            if (rc == ConfigResult.NOT_FOUND.value())
                return Optional.empty();
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            return Optional.of(adoptSpot(out.get(ValueLayout.ADDRESS, 0)));
        }
    }

    /**
     * Atomically gets the local logical spot for {@code spotRid}, creating it
     * when it is absent.
     */
    public SpotGetOrCreateResult getOrCreateSpot(RoutingId spotRid) {
        Objects.requireNonNull(spotRid, "spotRid");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment created = arena.allocate(ValueLayout.JAVA_INT);
            int rc = Native.spotNodeSpotGetOrNew(handle,
              ActorInterop.nativeRoutingId(arena, spotRid), out, created);
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            return new SpotGetOrCreateResult(
              adoptSpot(out.get(ValueLayout.ADDRESS, 0)),
              created.get(ValueLayout.JAVA_INT, 0) != 0);
        }
    }

    /** Creates one local Actor owned by this node. */
    public Actor createActor(String actorId) {
        return actorOperations.createActor(actorId);
    }

    Actor actor(String actorId) {
        return actorOperations.createActor(actorId);
    }

    /** Looks up a live local Actor ref by id. */
    public ActorRef actorLookup(String actorId) {
        return actorOperations.lookupActor(actorId);
    }

    /** Builds an unchecked remote Actor ref for request APIs. */
    /**
     * Async remote actor lookup. The returned builder is staged: callers
     * configure {@code timeout(...)} then submit via {@code submit()} or
     * {@code submit(callback)}.
     */
    public ActorLookupOperation remoteActorGetRef(RoutingId targetNodeRid,
                                           String actorId) {
        return actorOperations.remoteActorGetRef(targetNodeRid, actorId);
    }

    /**
     * Async destroy. Succeeds only when the Actor is in the Entry Spot.
     */
    public ActorDestroyOperation destroyActor(ActorRef actor) {
        return actorOperations.destroyActor(actor);
    }

    /**
     * Async user-Spot join builder. Completion delivers the final ActorRef,
     * joined Spot rid, join epoch, and reply parts. {@code destSpotRid} must
     * be a user Spot; the Entry Spot is not a valid target.
     */
    public ActorJoinOperation joinActor(ActorRef actor, RoutingId destNodeRid,
                                 RoutingId destSpotRid) {
        return actorOperations.joinActor(actor, destNodeRid, destSpotRid);
    }

    /** Builds a request-bearing join to {@code destNodeRid}'s Entry Spot. */
    public ActorJoinEntrySpotOperation joinActorEntrySpot(ActorRef actor,
                                                   RoutingId destNodeRid,
                                                   Message request) {
        return actorOperations.joinActorEntrySpot(actor, destNodeRid, request);
    }

    /** Async leave to the same node's Entry Spot. */
    public ActorLeaveOperation leaveActor(ActorRef actor, RoutingId currentSpotRid) {
        return actorOperations.leaveActor(actor, currentSpotRid);
    }

    /**
     * Actor-to-session relay builder. Fire-and-forget reverse send through the
     * Actor's bound STREAM session.
     */
    public SendOperation sendActorBoundSession(ActorRef actor) {
        return actorOperations.sendActorBoundSession(actor);
    }

    /**
     * Session-to-actor forward builder for a STREAM session route owned by
     * another source SpotNode.
     */
    public SendOperation forwardActorBoundSession(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        return actorOperations.forwardActorBoundSession(
            actor,
            sourceNodeRid,
            sourceSessionRid);
    }

    public void closeActorBoundSession(ActorRef actor, Duration timeout) {
        actorOperations.closeActorBoundSession(actor, timeout);
    }

    void sendHwm(int value) {
        optionsSupport.sendHwm(value);
    }

    void recvHwm(int value) {
        optionsSupport.recvHwm(value);
    }

    public AutoHwmProfile routerHwmProfile() {
        return optionsSupport.routerHwmProfile();
    }

    public void routerHwmProfile(AutoHwmProfile profile) {
        optionsSupport.routerHwmProfile(profile);
    }

    public int routerHighWaterMark() {
        return optionsSupport.routerHighWaterMark();
    }

    public void routerHighWaterMark(int value) {
        optionsSupport.routerHighWaterMark(value);
    }

    public AutoHwmProfile pubSubHwmProfile() {
        return optionsSupport.pubSubHwmProfile();
    }

    public void pubSubHwmProfile(AutoHwmProfile profile) {
        optionsSupport.pubSubHwmProfile(profile);
    }

    public int pubSubHighWaterMark() {
        return optionsSupport.pubSubHighWaterMark();
    }

    public void pubSubHighWaterMark(int value) {
        optionsSupport.pubSubHighWaterMark(value);
    }

    public int dispatchWorkersMin() {
        return optionsSupport.dispatchWorkersMin();
    }

    public void dispatchWorkersMin(int value) {
        optionsSupport.dispatchWorkersMin(value);
    }

    public int dispatchWorkersMax() {
        return optionsSupport.dispatchWorkersMax();
    }

    public void dispatchWorkersMax(int value) {
        optionsSupport.dispatchWorkersMax(value);
    }

    /** Returns the current node status snapshot. */
    public SpotNodeStatus status() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.SPOT_NODE_STATUS_LAYOUT);
            int rc = Native.spotNodeStatus(handle, out);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_status");
            }
            return SpotNodeSnapshots.statusFromNative(out);
        }
    }

    /** Returns the current peer snapshot. */
    public List<SpotNodePeerEntry> peers() {
        return readPeerEntries(null);
    }

    /** Returns peer entries matching the supplied filter. */
    public List<SpotNodePeerEntry> peersQuery(SpotNodePeerFilter filter) {
        Objects.requireNonNull(filter, "filter");
        return readPeerEntries(filter);
    }

    /** Returns the current subject snapshot. */
    public List<SpotNodeSubjectEntry> subjects() {
        return subjects(null);
    }

    /** Returns subject entries matching the supplied filter. */
    public List<SpotNodeSubjectEntry> subjects(
      SpotNodeSubjectFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : SpotNodeSnapshots.subjectFilterToNative(filter, arena);
            return SpotNodeSnapshotReader.read("zlink_spot_node_subjects",
                NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_LAYOUT,
                (entries, count) -> Native.spotNodeSubjects(handle,
                    nativeFilter, entries, count),
                SpotNodeSnapshots::subjectEntryFromNative);
        }
    }

    /** Returns diagnostic socket snapshot rows that exist on this node. */
    public List<SpotNodeSocketEntry> internalSockets() {
        return socketSnapshots(null);
    }

    List<SpotNodeSocketEntry> socketSnapshots() {
        return internalSockets();
    }

    public List<SpotNodeSpotEntry> spots() {
        return SpotNodeSnapshotReader.read("zlink_spot_node_spots",
            NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAYOUT,
            (entries, count) -> Native.spotNodeSpots(handle, entries, count),
            ActorInterop::spotEntryFromNative);
    }

    public List<SpotNodeActorEntry> actors() {
        return SpotNodeSnapshotReader.read("zlink_spot_node_actors",
            NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAYOUT,
            (entries, count) -> Native.spotNodeActors(handle, entries, count),
            ActorInterop::actorEntryFromNative);
    }

    /** Returns diagnostic socket snapshot rows matching the supplied filter. */
    public List<SpotNodeSocketEntry> internalSockets(
      SpotNodeSocketFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : SpotNodeSnapshots.socketFilterToNative(filter, arena);
            return SpotNodeSnapshotReader.read(
                "zlink_spot_node_internal_sockets",
                NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_ENTRY_LAYOUT,
                (entries, count) -> Native.spotNodeInternalSockets(handle,
                    nativeFilter, entries, count),
                SpotNodeSnapshots::socketEntryFromNative);
        }
    }

    List<SpotNodeSocketEntry> socketSnapshots(
      SpotNodeSocketFilter filter) {
        return internalSockets(filter);
    }

    @Override
    public void close() {
        List<Spot> ownedSpots;
        MemorySegment nodeHandle;
        synchronized (lifecycleLock) {
            if (isClosed())
                return;
            ownedSpots = List.copyOf(liveSpotsByHandle.values());
            liveSpots.clear();
            liveSpotsByHandle.clear();
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
        if (closeFailure != null) {
            throw closeFailure;
        }
    }

    void releaseSpot(Spot spot) {
        synchronized (lifecycleLock) {
            liveSpots.remove(spot);
            liveSpotsByHandle.values().removeIf(existing -> existing == spot);
        }
    }

    private Spot adoptSpot(MemorySegment spotHandle) {
        if (spotHandle == null || spotHandle.address() == 0)
            throw new ZlinkConfigException(ConfigResult.INVALID_HANDLE);
        synchronized (lifecycleLock) {
            Spot existing = liveSpotsByHandle.get(spotHandle.address());
            if (existing != null)
                return existing;
        }
        Spot spot = InternalAccess.spotAdoptOwned(this, spotHandle);
        synchronized (lifecycleLock) {
            if (isClosed()) {
                spot.close();
                throw new ZlinkConfigException(ConfigResult.INVALID_HANDLE);
            }
            registerSpot(spot);
        }
        return spot;
    }

    private void registerSpot(Spot spot) {
        liveSpots.add(spot);
        MemorySegment spotHandle = InternalAccess.spotHandle(spot);
        if (spotHandle != null && spotHandle.address() != 0) {
            liveSpotsByHandle.put(spotHandle.address(), spot);
        }
    }

    private boolean isClosed() {
        return handle == null || handle.address() == 0;
    }

    void ensureOpen() {
        if (isClosed()) {
            throw new IllegalStateException("spot node is closed");
        }
    }

    private List<SpotNodePeerEntry> readPeerEntries(SpotNodePeerFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : SpotNodeSnapshots.peerFilterToNative(filter, arena);
            return SpotNodeSnapshotReader.read("zlink_spot_node_peers",
                NativeLayouts.SPOT_NODE_PEER_ENTRY_LAYOUT,
                (entries, count) -> filter == null
                    ? Native.spotNodePeers(handle, entries, count)
                    : Native.spotNodePeersQuery(handle, nativeFilter, entries,
                        count),
                SpotNodeSnapshots::peerEntryFromNative);
        }
    }

    private static String requireChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        if (channelName.isEmpty()) {
            throw new IllegalArgumentException("channelName must not be empty");
        }
        return channelName;
    }

    static int timeoutMillis(Duration timeout) {
        return DurationConversions.timeoutMillis(timeout);
    }

}
