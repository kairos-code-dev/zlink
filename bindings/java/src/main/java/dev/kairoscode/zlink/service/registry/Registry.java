/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.service.registry.MemberPeerEntry;
import dev.kairoscode.zlink.service.registry.ServiceRole;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Registry service facade aligned to the current core bind/snapshot/query
 * model.
 */
public final class Registry implements AutoCloseable {
    private MemorySegment handle;

    /** Creates a registry handle owned by the supplied context. */
    public Registry(Context ctx) {
        this.handle = Native.registryNew(InternalAccess.contextHandle(ctx));
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_registry_new");
    }

    /** Binds the registry's PUB and ROUTER endpoints. */
    public void bind(String pubEndpoint, String routerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryBind(handle,
              NativeHelpers.toCString(arena, pubEndpoint),
              NativeHelpers.toCString(arena, routerEndpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_bind");
        }
    }

    /** Sets the registry numeric id. */
    public void setId(int id) {
        int rc = Native.registrySetId(handle, id);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_set_id");
    }

    /** Adds one peer registry PUB endpoint. */
    public void addPeer(String peerPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryAddPeer(handle,
              NativeHelpers.toCString(arena, peerPubEndpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_add_peer");
        }
    }

    /** Configures heartbeat interval and timeout in milliseconds. */
    public void setHeartbeat(int intervalMs, int timeoutMs) {
        int rc = Native.registrySetHeartbeat(handle, intervalMs, timeoutMs);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_set_heartbeat");
    }

    /** Configures the topology broadcast interval in milliseconds. */
    public void setBroadcastInterval(int intervalMs) {
        int rc = Native.registrySetBroadcastInterval(handle, intervalMs);
        if (rc != 0) {
            throw ZlinkException.fromLastError(
              "zlink_registry_set_broadcast_interval");
        }
    }

    /** Configures server TLS credentials for the registry endpoints. */
    public void setTlsServer(String certPem, String keyPem,
                             boolean requireClientCert) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.setTlsServer(handle,
              NativeHelpers.toCString(arena, certPem),
              NativeHelpers.toCString(arena, keyPem),
              requireClientCert ? 1 : 0);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_set_tls_server");
            }
        }
    }

    /** Configures client TLS credentials for registry peer links. */
    public void setTlsClient(String caCertPem, String hostname,
                             boolean trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.setTlsClient(handle,
              NativeHelpers.toCString(arena, caCertPem),
              NativeHelpers.toCString(arena, hostname),
              trustSystem ? 1 : 0);
            if (rc != 0) {
                throw ZlinkException.fromLastError("zlink_set_tls_client");
            }
        }
    }

    /** Returns a point-in-time registry status snapshot. */
    public RegistryStatus statusSnapshot() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.REGISTRY_STATUS_LAYOUT);
            int rc = Native.registryStatusSnapshot(handle, out);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_status_snapshot");
            return RegistryStatus.fromNative(out);
        }
    }

    /** Returns the current service summary snapshot. */
    public List<RegistryServiceSummaryEntry> serviceSummarySnapshot() {
        return serviceSummarySnapshot(null);
    }

    /** Returns the filtered service summary snapshot. */
    public List<RegistryServiceSummaryEntry> serviceSummarySnapshot(
      RegistryServiceSummaryFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : filter.toNative(arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.registryServiceSummarySnapshot(handle, nativeFilter,
              MemorySegment.NULL, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_registry_service_summary_snapshot");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.registryServiceSummarySnapshot(handle, nativeFilter,
              entries, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_registry_service_summary_snapshot");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride =
              NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT.byteSize();
            ArrayList<RegistryServiceSummaryEntry> out =
              new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(RegistryServiceSummaryEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns member peers for one service view. */
    public List<MemberPeerEntry> memberPeers(ServiceType serviceType,
                                             String serviceName) {
        Objects.requireNonNull(serviceType, "serviceType");
        Objects.requireNonNull(serviceName, "serviceName");
        int count = countMemberPeers(serviceType, serviceName);
        if (count == 0)
            return List.of();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment entries = arena.allocate(
              NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT, count);
            MemorySegment countOut = arena.allocate(ValueLayout.JAVA_LONG);
            countOut.set(ValueLayout.JAVA_LONG, 0, count);
            int rc = Native.registryMemberPeers(handle,
              (short) serviceType.getValue(),
              NativeHelpers.toCString(arena, serviceName), entries, countOut);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_member_peers");
            int actual = Math.min(count, boundedCount(
              countOut.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT.byteSize();
            ArrayList<MemberPeerEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(MemberPeerEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns the metadata blob for one registry member peer. */
    public byte[] memberPeerMetadata(ServiceType serviceType, String serviceName,
                                     ServiceRole serviceRole, String endpoint) {
        Objects.requireNonNull(serviceType, "serviceType");
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(serviceRole, "serviceRole");
        Objects.requireNonNull(endpoint, "endpoint");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment metadata = arena.allocate(NativeLayouts.MSG_LAYOUT);
            initMessage(metadata);
            try {
            int rc = Native.registryMemberPeerMetadata(handle,
              (short) serviceType.getValue(),
              NativeHelpers.toCString(arena, serviceName),
              (short) serviceRole.getValue(),
              NativeHelpers.toCString(arena, endpoint), metadata);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_registry_member_peer_metadata");
            }
            return readMessageBytes(metadata);
            } finally {
                NativeMsg.msgClose(metadata);
            }
        }
    }

    /** Returns the full current topology snapshot. */
    public List<RegistryTopologyEntry> topologySnapshot() {
        return readTopology(null);
    }

    /** Returns topology entries matching the supplied filter. */
    public List<RegistryTopologyEntry> topologyQuery(
      RegistryTopologyFilter filter) {
        Objects.requireNonNull(filter, "filter");
        return readTopology(filter);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.registryDestroy(handle);
        handle = MemorySegment.NULL;
    }

    private List<RegistryTopologyEntry> readTopology(
      RegistryTopologyFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : filter.toNative(arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = filter == null
              ? Native.registryTopologySnapshot(handle, MemorySegment.NULL,
                count)
              : Native.registryTopologyQuery(handle, nativeFilter,
                MemorySegment.NULL, count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(filter == null
                  ? "zlink_registry_topology_snapshot"
                  : "zlink_registry_topology_query");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = filter == null
              ? Native.registryTopologySnapshot(handle, entries, count)
              : Native.registryTopologyQuery(handle, nativeFilter, entries,
                count);
            if (rc != 0) {
                throw ZlinkException.fromLastError(filter == null
                  ? "zlink_registry_topology_snapshot"
                  : "zlink_registry_topology_query");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT.byteSize();
            ArrayList<RegistryTopologyEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(RegistryTopologyEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    private int countMemberPeers(ServiceType serviceType, String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.registryMemberPeers(handle,
              (short) serviceType.getValue(),
              NativeHelpers.toCString(arena, serviceName), MemorySegment.NULL,
              count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_member_peers");
            return boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
        }
    }

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }

    private static void initMessage(MemorySegment message) {
        int rc = NativeMsg.msgInit(message);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init");
    }

    private static byte[] readMessageBytes(MemorySegment message) {
        int size = Math.toIntExact(NativeMsg.msgSize(message));
        byte[] bytes = new byte[size];
        if (size > 0) {
            MemorySegment.copy(NativeMsg.msgData(message).reinterpret(size), 0,
              MemorySegment.ofArray(bytes), 0, size);
        }
        return bytes;
    }
}
