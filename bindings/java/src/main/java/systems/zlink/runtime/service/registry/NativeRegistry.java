/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.registry;

import systems.zlink.contracts.service.registry.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Registry service facade aligned to the current core bind/snapshot/query
 * model.
 */
public final class NativeRegistry implements Registry {
    public static final int OPTION_ID = 0x3801;
    public static final int OPTION_HEARTBEAT_INTERVAL_MS = 0x3802;
    public static final int OPTION_HEARTBEAT_TIMEOUT_MS = 0x3803;
    public static final int OPTION_BROADCAST_INTERVAL_MS = 0x3804;

    private MemorySegment handle;

    public static Registry create(Context ctx) {
        return new NativeRegistry(ctx);
    }

    /** Creates a registry handle owned by the supplied context. */
    NativeRegistry(Context ctx) {
        this.handle = Native.registryNew(InternalAccess.contextHandle(ctx));
        if (handle == null || handle.address() == 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_new");
    }

    /** Binds the registry's PUB and ROUTER endpoints. */
    public void bind(String pubEndpoint, String routerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryBind(handle,
              NativeHelpers.toCString(arena, pubEndpoint),
              NativeHelpers.toCString(arena, routerEndpoint));
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_bind");
        }
    }

    /** Sets the registry numeric id. */
    public void setId(int id) {
        int rc = setOptionRaw(OPTION_ID, id);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_set");
    }

    /** Sets one typed registry option. */
    public void setOption(int option, int value) {
        int rc = setOptionRaw(option, value);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_set");
    }

    /** Reads one typed registry option. */
    public int getOption(int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment valueOut = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment errorOut = arena.allocate(ValueLayout.JAVA_INT);
            int value = Native.registryGetOption(handle, option, valueOut,
              errorOut);
            int err = errorOut.get(ValueLayout.JAVA_INT, 0);
            if (err != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_get");
            return value;
        }
    }

    /** Adds one peer registry PUB endpoint. */
    public void addPeer(String peerPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryAddPeer(handle,
              NativeHelpers.toCString(arena, peerPubEndpoint));
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_add_peer");
        }
    }

    /** Configures heartbeat interval and timeout. */
    public void setHeartbeat(Duration interval, Duration timeout) {
        int intervalMs = toIntMillis(interval, "interval");
        int timeoutMs = toIntMillis(timeout, "timeout");
        setOption(OPTION_HEARTBEAT_INTERVAL_MS, intervalMs);
        setOption(OPTION_HEARTBEAT_TIMEOUT_MS, timeoutMs);
    }

    /** Configures the topology broadcast interval. */
    public void setBroadcastInterval(Duration interval) {
        int intervalMs = toIntMillis(interval, "interval");
        setOption(OPTION_BROADCAST_INTERVAL_MS, intervalMs);
    }

    private int setOptionRaw(int option, int value) {
        return Native.registrySetOption(handle, option, value);
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
                throw InternalAccess.zlinkExceptionFromLastError("zlink_set_tls_server");
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
                throw InternalAccess.zlinkExceptionFromLastError("zlink_set_tls_client");
            }
        }
    }

    /** Returns a point-in-time registry status snapshot. */
    public RegistryStatus status() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.REGISTRY_STATUS_LAYOUT);
            int rc = Native.registryStatus(handle, out);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_status");
            return NativeRegistryCodecs.statusFromNative(out);
        }
    }

    /** Returns the current service summary snapshot. */
    public List<RegistryServiceSummaryEntry> serviceSummary() {
        return serviceSummary(null);
    }

    /** Returns the filtered service summary snapshot. */
    public List<RegistryServiceSummaryEntry> serviceSummary(
      RegistryServiceSummaryFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : NativeRegistryCodecs.serviceSummaryFilterToNative(filter, arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.registryServiceSummary(handle, nativeFilter,
              MemorySegment.NULL, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_registry_service_summary");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.registryServiceSummary(handle, nativeFilter,
              entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_registry_service_summary");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride =
              NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT.byteSize();
            ArrayList<RegistryServiceSummaryEntry> out =
              new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(NativeRegistryCodecs.serviceSummaryEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns member peers for one channel view. */
    public List<MemberPeerEntry> memberPeers(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        int count = countMemberPeers(channelName);
        if (count == 0)
            return List.of();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment entries = arena.allocate(
              NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT, count);
            MemorySegment countOut = arena.allocate(ValueLayout.JAVA_LONG);
            countOut.set(ValueLayout.JAVA_LONG, 0, count);
            int rc = Native.registryMemberPeers(handle,
              NativeHelpers.toCString(arena, channelName), entries, countOut);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_member_peers");
            int actual = Math.min(count, boundedCount(
              countOut.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT.byteSize();
            ArrayList<MemberPeerEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(NativeRegistryCodecs.memberPeerEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns the full current topology snapshot. */
    public List<RegistryTopologyEntry> topology() {
        return readTopology(null);
    }

    /** Returns topology entries matching the supplied filter. */
    public List<RegistryTopologyEntry> topology(
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
              : NativeRegistryCodecs.topologyFilterToNative(filter, arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = filter == null
              ? Native.registryTopology(handle, MemorySegment.NULL,
                count)
              : Native.registryTopology(handle, nativeFilter,
                MemorySegment.NULL, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(filter == null
                  ? "zlink_registry_topology"
                  : "zlink_registry_topology");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = filter == null
              ? Native.registryTopology(handle, entries, count)
              : Native.registryTopology(handle, nativeFilter, entries,
                count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(filter == null
                  ? "zlink_registry_topology"
                  : "zlink_registry_topology");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT.byteSize();
            ArrayList<RegistryTopologyEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(NativeRegistryCodecs.topologyEntryFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    private int countMemberPeers(String channelName) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.registryMemberPeers(handle,
              NativeHelpers.toCString(arena, channelName), MemorySegment.NULL,
              count);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_member_peers");
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

    private static int toIntMillis(Duration duration, String name) {
        Objects.requireNonNull(duration, name);
        long nanos;
        try {
            nanos = duration.toNanos();
        } catch (ArithmeticException ex) {
            throw new IllegalArgumentException(name + " is too large", ex);
        }
        if (nanos % 1_000_000L != 0L) {
            throw new IllegalArgumentException(name + " must be millisecond aligned");
        }
        long millis = nanos / 1_000_000L;
        if (millis < 0 || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(
              name + " is outside native millisecond range");
        }
        return (int) millis;
    }

}
