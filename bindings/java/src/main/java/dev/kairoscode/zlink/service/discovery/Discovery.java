/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.discovery;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import dev.kairoscode.zlink.service.receiver.ReceiverInfo;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class Discovery implements AutoCloseable {
    private MemorySegment handle;

    public Discovery(Context ctx, ServiceType serviceType) {
        this.handle = Native.discoveryNew(ctx.handle(), (short) serviceType.getValue());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_discovery_new_typed");
    }

    public MemorySegment handle() {
        return handle;
    }

    public void connectRegistry(String registryEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryConnectRegistry(handle,
                NativeHelpers.toCString(arena, registryEndpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_connect_registry");
        }
    }

    public void subscribe(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoverySubscribe(handle,
                NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_subscribe");
        }
    }

    public void unsubscribe(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryUnsubscribe(handle,
                NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_unsubscribe");
        }
    }

    public void setSockOpt(SocketOption option, byte[] value) {
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(value, "value");
        throwSocketOptionNotSupported(option.toString());
    }

    public void setSockOpt(SocketOption option, int value) {
        Objects.requireNonNull(option, "option");
        throwSocketOptionNotSupported(option.toString());
    }

    public void setSockOpt(DiscoverySocketRole role, SocketOption option,
                           byte[] value) {
        validateRole(role);
        setSockOpt(option, value);
    }

    public void setSockOpt(DiscoverySocketRole role, SocketOption option,
                           int value) {
        validateRole(role);
        setSockOpt(option, value);
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        throwSocketOptionNotSupported(option.name());
    }

    public void setOption(SocketOptionKey<Long> option, long value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        throwSocketOptionNotSupported(option.name());
    }

    public void setOption(SocketOptionKey<String> option, String value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        throwSocketOptionNotSupported(option.name());
    }

    public void setOption(SocketOptionKey<byte[]> option, byte[] value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        throwSocketOptionNotSupported(option.name());
    }

    public void setOption(DiscoverySocketRole role,
                          SocketOptionKey<Integer> option,
                          int value) {
        validateRole(role);
        setOption(option, value);
    }

    public void setOption(DiscoverySocketRole role,
                          SocketOptionKey<Long> option,
                          long value) {
        validateRole(role);
        setOption(option, value);
    }

    public void setOption(DiscoverySocketRole role,
                          SocketOptionKey<String> option,
                          String value) {
        validateRole(role);
        setOption(option, value);
    }

    public void setOption(DiscoverySocketRole role,
                          SocketOptionKey<byte[]> option,
                          byte[] value) {
        validateRole(role);
        setOption(option, value);
    }

    public int receiverCount(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryProviderCount(handle,
                NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_discovery_receiver_count");
            return rc;
        }
    }

    public boolean serviceAvailable(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryServiceAvailable(handle,
                NativeHelpers.toCString(arena, serviceName));
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
            int rc = Native.discoveryGetProviders(handle,
                NativeHelpers.toCString(arena, serviceName), arr, cnt);
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
    }

    private static void validateOptionType(SocketOptionKey<?> option,
                                           SocketOptionValueType expected) {
        if (option.valueType() != expected) {
            throw new IllegalArgumentException(
              option.name() + " expects " + option.valueType()
                + ", not " + expected);
        }
    }

    private static void validateRole(DiscoverySocketRole role) {
        Objects.requireNonNull(role, "role");
        if (role != DiscoverySocketRole.SUB) {
            throw new IllegalArgumentException(
              "unsupported Discovery socket role: " + role);
        }
    }

    private void throwSocketOptionNotSupported(String optionName) {
        throw new UnsupportedOperationException(
          "Discovery socket option '" + optionName
            + "' is not supported by the current core API.");
    }
}
