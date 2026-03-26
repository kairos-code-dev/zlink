/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.service.discovery.Discovery;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** Lifecycle and topology facade for the current unified spot node model. */
public final class SpotNode implements AutoCloseable {
    private MemorySegment handle;

    /** Creates a spot node owned by the supplied context. */
    public SpotNode(Context ctx) {
        Objects.requireNonNull(ctx, "ctx");
        this.handle = Native.spotNodeNew(ctx.handle());
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
        int rc = Native.spotNodeAttachDiscovery(handle, discovery.handle());
        if (rc != 0) {
            throw ZlinkException.fromLastError(
              "zlink_spot_node_attach_discovery");
        }
    }

    /** Opens a service monitor for the spot node handle. */
    public ServiceMonitor monitorOpen(int events) {
        MemorySegment monitor = Native.serviceMonitorOpen(handle, events);
        if (monitor == null || monitor.address() == 0) {
            throw ZlinkException.fromLastError("zlink_service_monitor_open");
        }
        return new ServiceMonitor(monitor);
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
        if (handle == null || handle.address() == 0)
            return;
        Native.spotNodeDestroy(handle);
        handle = MemorySegment.NULL;
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
}
