/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.AdmissionState;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.service.discovery.Discovery;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/** Lifecycle and topology facade for the current unified spot node model. */
public final class SpotNode implements AutoCloseable {
    private static final int OPT_SNDHWM = 23;
    private static final int OPT_RCVHWM = 24;
    private final Object lifecycleLock = new Object();
    private final Set<Spot> liveSpots =
      Collections.newSetFromMap(new IdentityHashMap<>());
    private final ConcurrentHashMap<String, MemorySegment> manualChannelDealers =
      new ConcurrentHashMap<>();
    private MemorySegment handle;
    private final SpotNodeSocketOptions socketOptions = new SpotNodeSocketOptions();

    /** Creates a spot node owned by the supplied context. */
    public SpotNode(Context ctx) {
        Objects.requireNonNull(ctx, "ctx");
        this.handle = Native.spotNodeNew(InternalAccess.contextHandle(ctx));
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_spot_node_new");
    }

    MemorySegment handle() {
        return handle;
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
            manualChannelDealers.put(requireServiceName(channelName),
              InternalAccess.socketHandle(dealer));
        }
    }

    MemorySegment manualChannelDealerHandle(String channelName) {
        return manualChannelDealers.get(requireServiceName(channelName));
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
        byte[] value = rid.toBytes();
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
        manualChannelDealers.clear();
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

    private final class SpotNodeSocketOptions {
        void setPubIntOption(int optionId, int value) {
            setIntOption(Native.spotNodeDefaultPub(handle), optionId, value);
        }

        void setSubIntOption(int optionId, int value) {
            setIntOption(Native.spotNodeDefaultSub(handle), optionId, value);
        }

        private void setIntOption(MemorySegment socketHandle,
                                  int optionId,
                                  int value) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
                nativeValue.set(ValueLayout.JAVA_INT, 0, value);
                int rc = Native.setSockOpt(socketHandle, optionId,
                    nativeValue,
                    Integer.BYTES);
                if (rc != 0) {
                    throw ZlinkException.fromLastError("zlink_set_option");
                }
            }
        }
    }
}
