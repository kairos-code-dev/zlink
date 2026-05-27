/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.Context;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;

public final class RegistryQueryClient implements AutoCloseable {
    private MemorySegment handle;

    public RegistryQueryClient(Context ctx) {
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

    public List<RegistryTopologyEntry> snapshot() {
        return snapshot(null);
    }

    public List<RegistryTopologyEntry> snapshot(RegistryTopologyFilter filter) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeFilter = filter == null ? MemorySegment.NULL
              : filter.toNative(arena);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.registryQuerySnapshot(handle, nativeFilter,
              MemorySegment.NULL, count);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_query_snapshot");
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0)
                return List.of();
            MemorySegment entries = arena.allocate(
              NativeLayouts.REGISTRY_TOPOLOGY_ENTRY_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.registryQuerySnapshot(handle, nativeFilter, entries,
              count);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_registry_query_snapshot");
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

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.registryQueryDestroy(handle);
        handle = MemorySegment.NULL;
    }

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }
}
