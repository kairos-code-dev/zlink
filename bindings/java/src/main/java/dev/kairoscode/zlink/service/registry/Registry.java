/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class Registry implements AutoCloseable {
    private MemorySegment handle;
    private Arena sockOptArena = Arena.ofShared();
    private MemorySegment sockOptScratch = MemorySegment.NULL;
    private int sockOptScratchCapacity = 16;

    public Registry(Context ctx) {
        this.handle = Native.registryNew(ctx.handle());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_registry_new");
    }

    public void setEndpoints(String pubEndpoint, String routerEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registrySetEndpoints(handle,
                NativeHelpers.toCString(arena, pubEndpoint),
                NativeHelpers.toCString(arena, routerEndpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_set_endpoints");
        }
    }

    public void setId(int id) {
        int rc = Native.registrySetId(handle, id);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_set_id");
    }

    public void addPeer(String peerPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.registryAddPeer(handle,
                NativeHelpers.toCString(arena, peerPubEndpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_registry_add_peer");
        }
    }

    public void setHeartbeat(int intervalMs, int timeoutMs) {
        int rc = Native.registrySetHeartbeat(handle, intervalMs, timeoutMs);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_set_heartbeat");
    }

    public void setBroadcastInterval(int intervalMs) {
        int rc = Native.registrySetBroadcastInterval(handle, intervalMs);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_set_broadcast_interval");
    }

    public void setSockOpt(RegistrySocketRole role, SocketOption option, byte[] value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(value, "value");
        setSockOptBytes(role.getValue(), option.getValue(), value, 0,
            value.length);
    }

    public void setSockOpt(RegistrySocketRole role, SocketOption option, int value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        setSockOptInt(role.getValue(), option.getValue(), value);
    }

    public void setOption(RegistrySocketRole role,
                          SocketOptionKey<Integer> option,
                          int value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        setSockOptInt(role.getValue(), option.optionId(), value);
    }

    public void setOption(RegistrySocketRole role,
                          SocketOptionKey<Long> option,
                          long value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(role.getValue(), option.optionId(), value);
    }

    public void setOption(RegistrySocketRole role,
                          SocketOptionKey<String> option,
                          String value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        byte[] utf8 = Objects.requireNonNull(value, "value").getBytes(
          StandardCharsets.UTF_8);
        setSockOptBytes(role.getValue(), option.optionId(), utf8, 0,
            utf8.length);
    }

    public void setOption(RegistrySocketRole role,
                          SocketOptionKey<byte[]> option,
                          byte[] value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        setSockOptBytes(role.getValue(), option.optionId(), value, 0,
            value.length);
    }

    public void start() {
        int rc = Native.registryStart(handle);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_start");
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.registryDestroy(handle);
        handle = MemorySegment.NULL;
        closeArena(sockOptArena);
        sockOptArena = null;
        sockOptScratch = MemorySegment.NULL;
        sockOptScratchCapacity = 0;
    }

    private MemorySegment ensureSockOptScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        if (sockOptScratch.address() == 0 || sockOptScratchCapacity < length) {
            closeArena(sockOptArena);
            sockOptArena = Arena.ofShared();
            sockOptScratch = sockOptArena.allocate(length);
            sockOptScratchCapacity = length;
        }
        return sockOptScratch.asSlice(0, length);
    }

    private static void validateOptionType(SocketOptionKey<?> option,
                                           SocketOptionValueType expected) {
        if (option.valueType() != expected) {
            throw new IllegalArgumentException(
              option.name() + " expects " + option.valueType()
                + ", not " + expected);
        }
    }

    private void setSockOptRaw(int role, int optionId, MemorySegment value,
                               long len) {
        int rc = Native.registrySetSockOpt(handle, role, optionId, value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_registry_setsockopt");
    }

    private void setSockOptBytes(int role, int optionId, byte[] value,
                                 int offset, int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSockOptScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(role, optionId, buf, length);
    }

    private void setSockOptInt(int role, int optionId, int value) {
        MemorySegment buf = ensureSockOptScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(role, optionId, buf, Integer.BYTES);
    }

    private void setSockOptLong(int role, int optionId, long value) {
        MemorySegment buf = ensureSockOptScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(role, optionId, buf, Long.BYTES);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }
}
