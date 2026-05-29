/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.registry;

import systems.zlink.contracts.service.registry.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeListSnapshots;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;

public final class NativeRegistryQueryClient implements RegistryQueryClient {
    private MemorySegment handle;

    public static RegistryQueryClient create(Context ctx) {
        return new NativeRegistryQueryClient(ctx);
    }

    NativeRegistryQueryClient(Context ctx) {
        this.handle = Native.registryQueryClientNew(
            InternalAccess.contextHandle(ctx));
        if (handle == null || handle.address() == 0) {
            throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_query_client_new");
        }
    }

    public void connect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryQueryClientConnect(handle,
              NativeHelpers.toCString(arena, endpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_registry_query_client_connect");
            }
        }
    }

    public List<RegistryTopologyEntry> topology() {
        return topology(null);
    }

    public List<RegistryTopologyEntry> topology(RegistryTopologyFilter filter) {
        return NativeListSnapshots.read(
          NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT,
          "zlink_registry_query_client_topology",
          (arena, entries, count) -> {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : NativeRegistryCodecs.topologyFilterToNative(filter, arena);
            return Native.registryQuerySnapshot(handle, nativeFilter, entries,
              count);
          },
          NativeRegistryCodecs::topologyEntryFromNative);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.registryQueryDestroy(handle);
        handle = MemorySegment.NULL;
    }

}
