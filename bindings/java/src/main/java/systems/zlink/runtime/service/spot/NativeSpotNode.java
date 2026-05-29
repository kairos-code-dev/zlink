/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorDestroyOperation;
import systems.zlink.contracts.service.spot.ActorJoinCallbackSubmitOperation;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotCompletion;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotHandler;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotOperation;
import systems.zlink.contracts.service.spot.ActorJoinHandler;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ActorLookupHandler;
import systems.zlink.contracts.service.spot.ActorLookupOperation;
import systems.zlink.contracts.service.spot.ActorLookupResult;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SendSubmitOperation;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeActorEntry;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.contracts.service.spot.SpotNodeOptions;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodePeerFilter;
import systems.zlink.contracts.service.spot.SpotNodeSocketEntry;
import systems.zlink.contracts.service.spot.SpotNodeSocketFilter;
import systems.zlink.contracts.service.spot.SpotNodeSpotEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.service.spot.SpotNodeSubjectFilter;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMonitorStatuses;
import systems.zlink.runtime.nativeapi.NativeMessage;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;

/** Native-backed lifecycle and topology facade for the current unified spot node model. */
public final class NativeSpotNode implements SpotNode {
    private static final int OPT_SNDHWM = 23;
    private static final int OPT_RCVHWM = 24;
    private static final int OPT_ROUTER_HWM_PROFILE = 0x360E;
    private static final int OPT_ROUTER_HWM = 0x360F;
    private static final int OPT_PUBSUB_HWM_PROFILE = 0x3610;
    private static final int OPT_PUBSUB_HWM = 0x3611;
    private static final int OPT_DISPATCH_WORKERS_MIN = 0x3612;
    private static final int OPT_DISPATCH_WORKERS_MAX = 0x3613;
    private final Object lifecycleLock = new Object();
    private final Set<Spot> liveSpots =
      Collections.newSetFromMap(new IdentityHashMap<>());
    private final Map<Long, Spot> liveSpotsByHandle = new LinkedHashMap<>();
    private MemorySegment handle;
    private final SpotNodeSocketOptions socketOptions = new SpotNodeSocketOptions();

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

    /** Connects this node's routed router to a router-capable channel peer. */
    public void connectRouterChannelPeer(String channelName, String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeConnectRouterChannelPeer(handle,
              NativeHelpers.toCString(arena, requireChannelName(channelName)),
              NativeHelpers.toCString(arena, endpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_connect_router_channel_peer");
            }
        }
    }

    public void connectRouterChannelPeerRid(String channelName,
                                            RoutingId peerRid,
                                            String endpoint) {
        Objects.requireNonNull(peerRid, "peerRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            int rc = Native.spotNodeConnectRouterChannelPeerRid(handle,
              NativeHelpers.toCString(arena, requireChannelName(channelName)),
              nativeRid,
              NativeHelpers.toCString(arena, endpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_connect_router_channel_peer_rid");
            }
        }
    }

    /** Disconnects one manually connected router-capable channel peer. */
    public void disconnectRouterChannelPeer(String channelName,
                                            String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeDisconnectRouterChannelPeer(handle,
              NativeHelpers.toCString(arena, requireChannelName(channelName)),
              NativeHelpers.toCString(arena, endpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_disconnect_router_channel_peer");
            }
        }
    }

    /** Disconnects the router-capable channel peer identified by routing id. */
    public void disconnectRouterChannelPeerRid(String channelName,
                                               RoutingId peerRid) {
        Objects.requireNonNull(peerRid, "peerRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            int rc = Native.spotNodeDisconnectRouterChannelPeerRid(handle,
              NativeHelpers.toCString(arena, requireChannelName(channelName)),
              nativeRid);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_disconnect_router_channel_peer_rid");
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

    /** Attaches a discovery view for SPOT routes accepted from a channel. */
    public void attachSpotRouteChannelDiscovery(String channelName,
                                                Discovery discovery) {
        Objects.requireNonNull(discovery, "discovery");
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeAttachRouterChannelDiscovery(handle,
              NativeHelpers.toCString(arena, requireChannelName(channelName)),
              InternalAccess.discoveryHandle(discovery));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_attach_router_channel_discovery");
            }
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
            throw InternalAccess.zlinkExceptionFromLastError(
              "zlink_spot_node_attach_channel_dealer");
        }
    }

    /** Attaches one manually connected channel DEALER to the node. */
    public void attachChannelDealerManual(String channelName,
                                          DealerSocket dealer) {
        Objects.requireNonNull(dealer, "dealer");
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeAttachChannelDealerManual(handle,
              NativeHelpers.toCString(arena, requireChannelName(channelName)),
              InternalAccess.socketHandle(dealer));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
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
            throw InternalAccess.zlinkExceptionFromLastError(
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
        return actor(actorId);
    }

    Actor actor(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = Native.spotNodeActorNew(handle,
              NativeHelpers.toCString(arena, actorId), out);
            if (rc != 0) {
                throw new systems.zlink.contracts.errors.ZlinkConfigException(
                  systems.zlink.contracts.errors.ConfigResult.fromValue(rc));
            }
            return new NativeActor(this, ActorInterop.actorRefFromNative(out));
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
                throw new systems.zlink.contracts.errors.ZlinkConfigException(
                  systems.zlink.contracts.errors.ConfigResult.fromValue(rc));
            }
            return ActorInterop.actorRefFromNative(out);
        }
    }

    /** Builds an unchecked remote Actor ref for request APIs. */
    /**
     * Async remote actor lookup. The returned builder is staged: callers
     * configure {@code timeout(...)} then submit via {@code submitAsync()} or
     * {@code submit(callback)}.
     */
    public ActorLookupOperation remoteActorGetRef(RoutingId targetNodeRid,
                                           String actorId) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        ensureOpen();
        return new ActorLookupBuilder(targetNodeRid, actorId);
    }

    /**
     * Async destroy. Succeeds only when the Actor is in the Entry Spot.
     */
    public ActorDestroyOperation destroyActor(ActorRef actor) {
        Objects.requireNonNull(actor, "actor");
        ensureOpen();
        return new ActorDestroyBuilder(actor);
    }

    /**
     * Async user-Spot join builder. Completion delivers the final ActorRef,
     * joined Spot rid, join epoch, and reply parts. {@code destSpotRid} must
     * be a user Spot; the Entry Spot is not a valid target.
     */
    public ActorJoinOperation joinActor(ActorRef actor, RoutingId destNodeRid,
                                 RoutingId destSpotRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        ensureOpen();
        return new ActorJoinBuilder(actor, destNodeRid, destSpotRid);
    }

    /**
     * Message-less Entry Spot join builder. Completion delivers the final
     * ActorRef after the Actor is in {@code destNodeRid}'s Entry Spot.
     */
    public ActorJoinEntrySpotOperation joinActorEntrySpot(ActorRef actor,
                                                   RoutingId destNodeRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        ensureOpen();
        return new ActorJoinEntrySpotBuilder(actor, destNodeRid);
    }

    /** Async leave to the same node's Entry Spot. */
    public ActorLeaveOperation leaveActor(ActorRef actor, RoutingId currentSpotRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(currentSpotRid, "currentSpotRid");
        ensureOpen();
        return new ActorLeaveBuilder(actor, currentSpotRid);
    }

    /**
     * Actor-to-session relay builder. Fire-and-forget reverse send through the
     * Actor's bound STREAM session.
     */
    public SendOperation sendActorBoundSession(ActorRef actor) {
        Objects.requireNonNull(actor, "actor");
        ensureOpen();
        return new SendBoundSessionBuilder(actor);
    }

    public void closeActorBoundSession(ActorRef actor, Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorCloseBoundSession(handle,
              ActorInterop.actorRefToNative(arena, actor),
              timeoutMillis(timeout));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_actor_close_bound_session");
            }
        }
    }

    void sendHwm(int value) {
        socketOptions.setPubIntOption(OPT_SNDHWM, value);
    }

    void recvHwm(int value) {
        socketOptions.setSubIntOption(OPT_RCVHWM, value);
    }

    public AutoHwmProfile routerHwmProfile() {
        return EnumCodecs.autoHwmProfileFromValue(
          getIntOption(OPT_ROUTER_HWM_PROFILE));
    }

    public void routerHwmProfile(AutoHwmProfile profile) {
        Objects.requireNonNull(profile, "profile");
        setIntOption(OPT_ROUTER_HWM_PROFILE,
          EnumCodecs.autoHwmProfileValue(profile));
    }

    public int routerHighWaterMark() {
        return getIntOption(OPT_ROUTER_HWM);
    }

    public void routerHighWaterMark(int value) {
        setIntOption(OPT_ROUTER_HWM, value);
    }

    public AutoHwmProfile pubSubHwmProfile() {
        return EnumCodecs.autoHwmProfileFromValue(
          getIntOption(OPT_PUBSUB_HWM_PROFILE));
    }

    public void pubSubHwmProfile(AutoHwmProfile profile) {
        Objects.requireNonNull(profile, "profile");
        setIntOption(OPT_PUBSUB_HWM_PROFILE,
          EnumCodecs.autoHwmProfileValue(profile));
    }

    public int pubSubHighWaterMark() {
        return getIntOption(OPT_PUBSUB_HWM);
    }

    public void pubSubHighWaterMark(int value) {
        setIntOption(OPT_PUBSUB_HWM, value);
    }

    public int dispatchWorkersMin() {
        return getIntOption(OPT_DISPATCH_WORKERS_MIN);
    }

    public void dispatchWorkersMin(int value) {
        setIntOption(OPT_DISPATCH_WORKERS_MIN, value);
    }

    public int dispatchWorkersMax() {
        return getIntOption(OPT_DISPATCH_WORKERS_MAX);
    }

    public void dispatchWorkersMax(int value) {
        setIntOption(OPT_DISPATCH_WORKERS_MAX, value);
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
            return statusFromNative(out);
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
              : subjectFilterToNative(filter, arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeSubjects(handle, nativeFilter,
              MemorySegment.NULL, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_subjects");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeSubjects(handle, nativeFilter, entries,
              count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_subjects");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride =
              NativeLayouts.SPOT_NODE_SUBJECT_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodeSubjectEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(subjectEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
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
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeSpots(handle, MemorySegment.NULL,
              count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_spots");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_SPOT_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeSpots(handle, entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_spots");
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

    public List<SpotNodeActorEntry> actors() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeActors(handle, MemorySegment.NULL,
              count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_actors");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_ACTOR_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeActors(handle, entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_actors");
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

    /** Returns diagnostic socket snapshot rows matching the supplied filter. */
    public List<SpotNodeSocketEntry> internalSockets(
      SpotNodeSocketFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : socketFilterToNative(filter, arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotNodeInternalSockets(handle,
              nativeFilter, MemorySegment.NULL, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_internal_sockets");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotNodeInternalSockets(handle, nativeFilter,
              entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_spot_node_internal_sockets");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride =
              NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodeSocketEntry> out =
              new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(socketEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
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

    private int getIntOption(int option) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSpotNodeOption(handle, option, nativeValue, len);
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            return nativeValue.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private void setIntOption(int option, int value) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setSpotNodeOption(handle, option, nativeValue,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
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
              : peerFilterToNative(filter, arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = filter == null
              ? Native.spotNodePeers(handle, MemorySegment.NULL, count)
              : Native.spotNodePeersQuery(handle, nativeFilter,
                MemorySegment.NULL, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(filter == null
                  ? "zlink_spot_node_peers"
                  : "zlink_spot_node_peers");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.SPOT_NODE_PEER_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = filter == null
              ? Native.spotNodePeers(handle, entries, count)
              : Native.spotNodePeersQuery(handle, nativeFilter, entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(filter == null
                  ? "zlink_spot_node_peers"
                  : "zlink_spot_node_peers");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.SPOT_NODE_PEER_ENTRY_LAYOUT.byteSize();
            ArrayList<SpotNodePeerEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(peerEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    private static SpotNodeStatus statusFromNative(MemorySegment segment) {
        int routingSize = segment.get(ValueLayout.JAVA_BYTE, 512) & 0xFF;
        byte[] routing = new byte[routingSize];
        if (routingSize > 0) {
            MemorySegment.copy(segment, 513, MemorySegment.ofArray(routing), 0,
              routingSize);
        }
        return new SpotNodeStatus(
          NativeHelpers.fromCString(segment.asSlice(0, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(256, 256), 256),
          RoutingId.from(routing),
          EnumCodecs.spotNodeStateFromValue(segment.get(ValueLayout.JAVA_INT, 768)),
          segment.get(ValueLayout.JAVA_INT, 772),
          segment.get(ValueLayout.JAVA_INT, 776),
          segment.get(ValueLayout.JAVA_INT, 780),
          segment.get(ValueLayout.JAVA_INT, 784),
          segment.get(ValueLayout.JAVA_INT, 788),
          segment.get(ValueLayout.JAVA_INT, 792),
          segment.get(ValueLayout.JAVA_INT, 796),
          segment.get(ValueLayout.JAVA_INT, 800),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 808));
    }

    private static MemorySegment subjectFilterToNative(
      SpotNodeSubjectFilter filter,
      Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SUBJECT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          filter.role() == null ? 0 : EnumCodecs.spotRoleValue(filter.role()));
        if (filter.subject() != null && !filter.subject().isEmpty()) {
            MemorySegment nativeSubject =
              NativeHelpers.toCString(arena, filter.subject());
            MemorySegment.copy(nativeSubject, 0, segment, 4,
              Math.min(nativeSubject.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT, 260,
          filter.subjectKind() == null ? 0
            : EnumCodecs.serviceEventSubjectKindValue(filter.subjectKind()));
        return segment;
    }

    private static SpotNodeSubjectEntry subjectEntryFromNative(
      MemorySegment segment) {
        return new SpotNodeSubjectEntry(
          EnumCodecs.spotRoleFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          NativeHelpers.fromCString(segment.asSlice(4, 256), 256),
          EnumCodecs.serviceEventSubjectKindFromValue(
              segment.get(ValueLayout.JAVA_INT, 260)),
          segment.get(ValueLayout.JAVA_INT, 264),
          segment.get(ValueLayout.JAVA_INT, 268),
          segment.get(ValueLayout.JAVA_LONG, 272));
    }

    private static MemorySegment socketFilterToNative(
      SpotNodeSocketFilter filter,
      Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          filter.owner() == null ? 0
            : EnumCodecs.spotNodeSocketOwnerValue(filter.owner()));
        segment.set(ValueLayout.JAVA_INT, 4,
          filter.socketType() == null ? 0
            : EnumCodecs.socketTypeValue(filter.socketType()));
        if (filter.socketName() != null && !filter.socketName().isEmpty()) {
            MemorySegment nativeName =
              NativeHelpers.toCString(arena, filter.socketName());
            MemorySegment.copy(nativeName, 0, segment, 8,
              Math.min(nativeName.byteSize(), 64));
        }
        return segment;
    }

    private static SpotNodeSocketEntry socketEntryFromNative(
      MemorySegment segment) {
        return new SpotNodeSocketEntry(
          EnumCodecs.spotNodeSocketOwnerFromValue(
              segment.get(ValueLayout.JAVA_INT, 0)),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 8),
          NativeHelpers.fromCString(segment.asSlice(16, 64), 64),
          NativeHelpers.fromCString(segment.asSlice(80, 64), 64),
          EnumCodecs.socketTypeFromValue(segment.get(ValueLayout.JAVA_INT, 144)),
          segment.get(ValueLayout.JAVA_INT, 148) != 0,
          NativeMonitorStatuses.fromNative(segment.asSlice(152,
            NativeLayouts.MONITOR_SNAPSHOT_LAYOUT.byteSize())));
    }

    private static MemorySegment peerFilterToNative(SpotNodePeerFilter filter,
                                                    Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_PEER_FILTER_LAYOUT);
        if (filter.peerEndpoint() != null && !filter.peerEndpoint().isEmpty()) {
            MemorySegment endpoint =
              NativeHelpers.toCString(arena, filter.peerEndpoint());
            MemorySegment.copy(endpoint, 0, segment, 0,
              Math.min(endpoint.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT, 256,
          filter.source() == null ? 0
            : EnumCodecs.spotPeerSourceValue(filter.source()));
        segment.set(ValueLayout.JAVA_INT, 260,
          filter.state() == null ? 0
            : EnumCodecs.spotPeerStateValue(filter.state()));
        return segment;
    }

    private static SpotNodePeerEntry peerEntryFromNative(MemorySegment segment) {
        return new SpotNodePeerEntry(
          NativeHelpers.fromCString(segment.asSlice(0, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(256, 256), 256),
          NativeHelpers.fromCString(segment.asSlice(512, 256), 256),
          EnumCodecs.spotPeerSourceFromValue(segment.get(ValueLayout.JAVA_INT, 768)),
          EnumCodecs.spotPeerKindFromValue(segment.get(ValueLayout.JAVA_INT, 772)),
          EnumCodecs.spotPeerStateFromValue(segment.get(ValueLayout.JAVA_INT, 776)),
          segment.get(ValueLayout.JAVA_INT, 780),
          segment.get(ValueLayout.JAVA_LONG, 784),
          segment.get(ValueLayout.JAVA_LONG, 792));
    }

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }

    private static String requireChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        if (channelName.isEmpty()) {
            throw new IllegalArgumentException("channelName must not be empty");
        }
        return channelName;
    }

    private static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }

    private final class ActorLookupBuilder implements ActorLookupOperation {
        private final RoutingId targetNodeRid;
        private final String actorId;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorLookupBuilder(RoutingId targetNodeRid, String actorId) {
            this.targetNodeRid = targetNodeRid;
            this.actorId = actorId;
        }

        @Override
        public ActorLookupOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<ActorLookupResult> submitAsync() {
            CompletableFuture<ActorLookupResult> future = new CompletableFuture<>();
            submit(result -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(result);
                } else {
                    future.completeExceptionally(new ZlinkRequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorLookupHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.LookupPendingToken token =
              ActorRequestCallbacks.registerLookup(callback);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.remoteActorGetRef(handle,
                  ActorInterop.nativeRoutingId(arena, targetNodeRid),
                  NativeHelpers.toCString(arena, actorId),
                  ActorRequestCallbacks.ACTOR_LOOKUP_CALLBACK,
                  MemorySegment.ofAddress(token.id()), timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorDestroyBuilder implements ActorDestroyOperation {
        private final ActorRef actor;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorDestroyBuilder(ActorRef actor) {
            this.actor = actor;
        }

        @Override
        public ActorDestroyOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<List<Message>> submitAsync() {
            CompletableFuture<List<Message>> future = new CompletableFuture<>();
            submit((result, parts) -> {
                if (result == RequestResult.OK) {
                    future.complete(parts);
                } else {
                    future.completeExceptionally(new ZlinkRequestException(result));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ReplyHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.PendingToken token =
              ActorRequestCallbacks.register(callback::onReply);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.spotNodeActorDestroy(handle,
                  ActorInterop.actorRefToNative(arena, actor),
                  ActorRequestCallbacks.REPLY_CALLBACK,
                  MemorySegment.ofAddress(token.id()),
                  timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorLeaveBuilder implements ActorLeaveOperation {
        private final ActorRef actor;
        private final RoutingId currentSpotRid;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorLeaveBuilder(ActorRef actor, RoutingId currentSpotRid) {
            this.actor = actor;
            this.currentSpotRid = currentSpotRid;
        }

        @Override
        public ActorLeaveOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<List<Message>> submitAsync() {
            CompletableFuture<List<Message>> future = new CompletableFuture<>();
            submit((result, parts) -> {
                if (result == RequestResult.OK) {
                    future.complete(parts);
                } else {
                    future.completeExceptionally(new ZlinkRequestException(result));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ReplyHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.PendingToken token =
              ActorRequestCallbacks.register(callback::onReply);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.spotNodeActorLeaveSpot(handle,
                  ActorInterop.actorRefToNative(arena, actor),
                  ActorInterop.nativeRoutingId(arena, currentSpotRid),
                  ActorRequestCallbacks.REPLY_CALLBACK,
                  MemorySegment.ofAddress(token.id()),
                  timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorJoinBuilder
      implements ActorJoinOperation, ActorJoinSubmitOperation {
        private final ActorRef actor;
        private final RoutingId destNodeRid;
        private final RoutingId destSpotRid;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private Duration timeout = Duration.ofMillis(5_000L);
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        ActorJoinBuilder(ActorRef actor, RoutingId destNodeRid,
                         RoutingId destSpotRid) {
            this.actor = actor;
            this.destNodeRid = destNodeRid;
            this.destSpotRid = destSpotRid;
        }

        @Override
        public ActorJoinSubmitOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public ActorJoinSubmitOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return new ActorJoinCallbackStage(this);
        }

        @Override
        public CompletableFuture<ActorJoinCompletion> submitAsync() {
            CompletableFuture<ActorJoinCompletion> future = new CompletableFuture<>();
            submit((result, replyParts) -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(new ActorJoinCompletion(result, replyParts));
                } else {
                    future.completeExceptionally(new ZlinkRequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorJoinHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            ActorRequestCallbacks.JoinPendingToken token =
              ActorRequestCallbacks.registerJoin(callback);
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment partsArr = parts.copyToNativeArray(arena);
                int rc = Native.spotNodeActorJoinSpot(handle,
                  ActorInterop.actorRefToNative(arena, actor),
                  ActorInterop.nativeRoutingId(arena, destNodeRid),
                  ActorInterop.nativeRoutingId(arena, destSpotRid),
                  partsArr, parts.size(),
                  ActorRequestCallbacks.ACTOR_JOIN_CALLBACK,
                  MemorySegment.ofAddress(token.id()), flags.value(),
                  timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    MessagePartsBuffer.closeNativeArray(partsArr, parts.size());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorJoinCallbackStage
      implements ActorJoinCallbackSubmitOperation {
        private final ActorJoinBuilder builder;

        ActorJoinCallbackStage(ActorJoinBuilder builder) {
            this.builder = builder;
        }

        @Override
        public ActorJoinCallbackSubmitOperation message(Message part) {
            builder.message(part);
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOperation timeout(Duration timeout) {
            builder.timeout(timeout);
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOperation flags(SendFlags flags) {
            builder.flags(flags);
            return this;
        }

        @Override
        public boolean submit(ActorJoinHandler callback) {
            return builder.submit(callback);
        }
    }

    private final class ActorJoinEntrySpotBuilder
      implements ActorJoinEntrySpotOperation {
        private final ActorRef actor;
        private final RoutingId destNodeRid;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorJoinEntrySpotBuilder(ActorRef actor, RoutingId destNodeRid) {
            this.actor = actor;
            this.destNodeRid = destNodeRid;
        }

        @Override
        public ActorJoinEntrySpotOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<ActorJoinEntrySpotCompletion> submitAsync() {
            CompletableFuture<ActorJoinEntrySpotCompletion> future =
              new CompletableFuture<>();
            submit(result -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(new ActorJoinEntrySpotCompletion(result));
                } else {
                    future.completeExceptionally(
                      new ZlinkRequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorJoinEntrySpotHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.JoinEntrySpotPendingToken token =
              ActorRequestCallbacks.registerJoinEntrySpot(callback);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.spotNodeActorJoinEntrySpot(handle,
                  ActorInterop.actorRefToNative(arena, actor),
                  ActorInterop.nativeRoutingId(arena, destNodeRid),
                  ActorRequestCallbacks.ACTOR_JOIN_ENTRY_SPOT_CALLBACK,
                  MemorySegment.ofAddress(token.id()), timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class SendBoundSessionBuilder
      implements SendOperation, SendSubmitOperation {
        private final ActorRef actor;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        SendBoundSessionBuilder(ActorRef actor) {
            this.actor = actor;
        }

        @Override
        public SendSubmitOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public SendSubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment refSegment = ActorInterop.actorRefToNative(arena,
                  actor);
                for (int i = 0; i < parts.size(); i++) {
                    Message part = parts.get(i);
                    MemorySegment nativeMsg = arena.allocate(
                      NativeLayouts.MESSAGE_LAYOUT);
                    InternalAccess.messageCopyTo(part, nativeMsg);
                    int rc = Native.spotNodeActorSendBoundSessionMessage(handle,
                      refSegment, nativeMsg, flags.value());
                    if (rc != 0) {
                        NativeMessage.messageClose(nativeMsg);
                        if (flags == SendFlags.DONT_WAIT
                            && SubmitResult.fromValue(rc) == SubmitResult.BACKPRESSURED) {
                            return false;
                        }
                        throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                    }
                }
            }
            return true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
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
                    throw InternalAccess.zlinkExceptionFromLastError(publishOption
                      ? "zlink_set_pub_option"
                      : "zlink_set_sub_option");
                }
            }
        }
    }
}
