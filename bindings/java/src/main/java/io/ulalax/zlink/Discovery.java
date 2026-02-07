package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import io.ulalax.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Discovery implements AutoCloseable {
    public static final short SERVICE_TYPE_GATEWAY = 1;
    public static final short SERVICE_TYPE_SPOT = 2;

    private MemorySegment handle;

    public Discovery(Context ctx, short serviceType) {
        this.handle = Native.discoveryNew(ctx.handle(), serviceType);
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_discovery_new_typed failed");
    }

    MemorySegment handle() {
        return handle;
    }

    public void connectRegistry(String registryPubEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryConnectRegistry(handle,
                NativeHelpers.toCString(arena, registryPubEndpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_discovery_connect_registry failed");
        }
    }

    public void subscribe(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoverySubscribe(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw new RuntimeException("zlink_discovery_subscribe failed");
        }
    }

    public void unsubscribe(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryUnsubscribe(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw new RuntimeException("zlink_discovery_unsubscribe failed");
        }
    }

    public void setSockOpt(int role, int option, byte[] value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(value.length);
            MemorySegment.copy(MemorySegment.ofArray(value), 0, buf, 0, value.length);
            int rc = Native.discoverySetSockOpt(handle, role, option, buf, value.length);
            if (rc != 0)
                throw new RuntimeException("zlink_discovery_setsockopt failed");
        }
    }

    public void setSockOpt(int role, int option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.discoverySetSockOpt(handle, role, option, buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_discovery_setsockopt failed");
        }
    }

    public int receiverCount(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryProviderCount(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw new RuntimeException("zlink_discovery_receiver_count failed");
            return rc;
        }
    }

    public boolean serviceAvailable(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryServiceAvailable(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw new RuntimeException("zlink_discovery_service_available failed");
            return rc != 0;
        }
    }

    public ReceiverInfo[] getReceivers(String serviceName) {
        int count = receiverCount(serviceName);
        if (count <= 0)
            return new ReceiverInfo[0];
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment arr = arena.allocate(NativeLayouts.PROVIDER_INFO_LAYOUT, count);
            MemorySegment cnt = arena.allocate(ValueLayout.JAVA_LONG);
            cnt.set(ValueLayout.JAVA_LONG, 0, count);
            int rc = Native.discoveryGetProviders(handle, NativeHelpers.toCString(arena, serviceName), arr, cnt);
            if (rc != 0)
                throw new RuntimeException("zlink_discovery_get_receivers failed");
            long actual = cnt.get(ValueLayout.JAVA_LONG, 0);
            ReceiverInfo[] out = new ReceiverInfo[(int) actual];
            for (int i = 0; i < actual; i++) {
                MemorySegment item = arr.asSlice((long) i * NativeLayouts.PROVIDER_INFO_LAYOUT.byteSize(),
                    NativeLayouts.PROVIDER_INFO_LAYOUT.byteSize());
                out[i] = ReceiverInfo.from(item);
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
}
