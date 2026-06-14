/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.registry;

import systems.zlink.contracts.service.registry.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.contracts.internal.DurationConversions;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeListSnapshots;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;

/**
 * Registry service facade aligned to the current core bind/snapshot/query
 * model.
 */
public final class NativeRegistry implements Registry {
    private static final int OPTION_ID = 0x3801;
    private static final int OPTION_HEARTBEAT_INTERVAL_MS = 0x3802;
    private static final int OPTION_HEARTBEAT_TIMEOUT_MS = 0x3803;
    private static final int OPTION_BROADCAST_INTERVAL_MS = 0x3804;

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
    private void setOption(int option, int value) {
        int rc = setOptionRaw(option, value);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_set");
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
        int intervalMs = DurationConversions.toNativeTimeoutMillis(interval,
            "interval");
        int timeoutMs = DurationConversions.toNativeTimeoutMillis(timeout,
            "timeout");
        setOption(OPTION_HEARTBEAT_INTERVAL_MS, intervalMs);
        setOption(OPTION_HEARTBEAT_TIMEOUT_MS, timeoutMs);
    }

    /** Configures the topology broadcast interval. */
    public void setBroadcastInterval(Duration interval) {
        int intervalMs = DurationConversions.toNativeTimeoutMillis(interval,
            "interval");
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
        return NativeListSnapshots.read(
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT,
          "zlink_registry_service_summary",
          (arena, entries, count) -> {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : NativeRegistryCodecs.serviceSummaryFilterToNative(filter, arena);
            return Native.registryServiceSummary(handle, nativeFilter,
              entries, count);
          },
          NativeRegistryCodecs::serviceSummaryEntryFromNative);
    }

    /** Returns member peers for one channel view. */
    public List<MemberPeerEntry> memberPeers(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        return NativeListSnapshots.read(
          NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT,
          "zlink_registry_member_peers",
          (arena, entries, count) -> Native.registryMemberPeers(handle,
            NativeHelpers.toCString(arena, channelName), entries, count),
          NativeRegistryCodecs::memberPeerEntryFromNative);
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
        return NativeListSnapshots.read(
          NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT,
          "zlink_registry_topology",
          (arena, entries, count) -> {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : NativeRegistryCodecs.topologyFilterToNative(filter, arena);
            return filter == null
              ? Native.registryTopology(handle, entries, count)
              : Native.registryTopology(handle, nativeFilter, entries,
                count);
          },
          NativeRegistryCodecs::topologyEntryFromNative);
    }

}
