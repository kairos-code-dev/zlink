/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Registry implements AutoCloseable {
    private MemorySegment handle;

    public Registry(Context ctx) {
        this.handle = Native.registryNew(ctx.handle());
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_registry_new failed");
    }

    public void setEndpoints(String pubEndpoint, String routerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registrySetEndpoints(handle,
                NativeHelpers.toCString(arena, pubEndpoint),
                NativeHelpers.toCString(arena, routerEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_registry_set_endpoints failed");
        }
    }

    public void setId(int id) {
        int rc = Native.registrySetId(handle, id);
        if (rc != 0)
            throw new RuntimeException("zlink_registry_set_id failed");
    }

    public void addPeer(String peerPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryAddPeer(handle,
                NativeHelpers.toCString(arena, peerPubEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_registry_add_peer failed");
        }
    }

    public void setHeartbeat(int intervalMs, int timeoutMs) {
        int rc = Native.registrySetHeartbeat(handle, intervalMs, timeoutMs);
        if (rc != 0)
            throw new RuntimeException("zlink_registry_set_heartbeat failed");
    }

    public void setBroadcastInterval(int intervalMs) {
        int rc = Native.registrySetBroadcastInterval(handle, intervalMs);
        if (rc != 0)
            throw new RuntimeException("zlink_registry_set_broadcast_interval failed");
    }

    public void setSockOpt(RegistrySocketRole role, SocketOption option, byte[] value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(value.length);
            MemorySegment.copy(MemorySegment.ofArray(value), 0, buf, 0, value.length);
            int rc = Native.registrySetSockOpt(handle, role.getValue(), option.getValue(), buf, value.length);
            if (rc != 0)
                throw new RuntimeException("zlink_registry_setsockopt failed");
        }
    }

    public void setSockOpt(RegistrySocketRole role, SocketOption option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.registrySetSockOpt(handle, role.getValue(), option.getValue(), buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_registry_setsockopt failed");
        }
    }

    public void start() {
        int rc = Native.registryStart(handle);
        if (rc != 0)
            throw new RuntimeException("zlink_registry_start failed");
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.registryDestroy(handle);
        handle = MemorySegment.NULL;
    }
}
