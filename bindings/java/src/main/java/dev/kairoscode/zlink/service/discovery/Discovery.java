/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.discovery;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import dev.kairoscode.zlink.service.receiver.ReceiverInfo;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class Discovery implements AutoCloseable {
    private MemorySegment handle;
    private Arena sockOptArena = Arena.ofShared();
    private MemorySegment sockOptScratch = MemorySegment.NULL;
    private int sockOptScratchCapacity = 16;

    public Discovery(Context ctx, ServiceType serviceType) {
        this.handle = Native.discoveryNew(ctx.handle(), (short) serviceType.getValue());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_discovery_new_typed");
    }

    public MemorySegment handle() {
        return handle;
    }

    public void connectRegistry(String registryPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryConnectRegistry(handle,
                NativeHelpers.toCString(arena, registryPubEndpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_connect_registry");
        }
    }

    public void subscribe(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoverySubscribe(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_subscribe");
        }
    }

    public void unsubscribe(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryUnsubscribe(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_unsubscribe");
        }
    }

    public void setSockOpt(DiscoverySocketRole role, SocketOption option, byte[] value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(value, "value");
        setSockOptBytes(role.getValue(), option.getValue(), value, 0,
            value.length);
    }

    public void setSockOpt(DiscoverySocketRole role, SocketOption option, int value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        setSockOptInt(role.getValue(), option.getValue(), value);
    }

    public void setOption(DiscoverySocketRole role,
                          SocketOptionKey<Integer> option,
                          int value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        setSockOptInt(role.getValue(), option.optionId(), value);
    }

    public void setOption(DiscoverySocketRole role,
                          SocketOptionKey<Long> option,
                          long value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(role.getValue(), option.optionId(), value);
    }

    public void setOption(DiscoverySocketRole role,
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

    public void setOption(DiscoverySocketRole role,
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

    public int receiverCount(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryProviderCount(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_discovery_receiver_count");
            return rc;
        }
    }

    public boolean serviceAvailable(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryServiceAvailable(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_discovery_service_available");
            return rc != 0;
        }
    }

    public List<ReceiverInfo> getReceivers(String serviceName) {
        int count = receiverCount(serviceName);
        if (count <= 0)
            return Collections.emptyList();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment arr = arena.allocate(NativeLayouts.PROVIDER_INFO_LAYOUT, count);
            MemorySegment cnt = arena.allocate(ValueLayout.JAVA_LONG);
            cnt.set(ValueLayout.JAVA_LONG, 0, count);
            int rc = Native.discoveryGetProviders(handle, NativeHelpers.toCString(arena, serviceName), arr, cnt);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_get_receivers");
            long actualLong = cnt.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > count)
                actualLong = count;
            int actual = (int) actualLong;
            ArrayList<ReceiverInfo> out = new ArrayList<>(actual);
            long stride = NativeLayouts.PROVIDER_INFO_LAYOUT.byteSize();
            for (int i = 0; i < actual; i++) {
                MemorySegment item = arr.asSlice((long) i * stride, stride);
                out.add(ReceiverInfo.from(item));
            }
            return out;
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.discoveryDestroy(handle);
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
        int rc = Native.discoverySetSockOpt(handle, role, optionId, value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_discovery_setsockopt");
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
