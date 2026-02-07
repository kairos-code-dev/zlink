package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Provider implements AutoCloseable {
    private MemorySegment handle;

    public Provider(Context ctx) {
        this(ctx, null);
    }

    public Provider(Context ctx, String routingId) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = routingId == null ? MemorySegment.NULL
                : NativeHelpers.toCString(arena, routingId);
            this.handle = Native.providerNew(ctx.handle(), rid);
        }
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_provider_new failed");
    }

    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerBind(handle, NativeHelpers.toCString(arena, endpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_provider_bind failed");
        }
    }

    public void connectRegistry(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerConnectRegistry(handle, NativeHelpers.toCString(arena, endpoint));
            if (rc != 0)
                throw new RuntimeException("zlink_provider_connect_registry failed");
        }
    }

    public void register(String serviceName, String advertiseEndpoint, int weight) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerRegister(handle,
                NativeHelpers.toCString(arena, serviceName),
                NativeHelpers.toCString(arena, advertiseEndpoint),
                weight);
            if (rc != 0)
                throw new RuntimeException("zlink_provider_register failed");
        }
    }

    public void updateWeight(String serviceName, int weight) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerUpdateWeight(handle,
                NativeHelpers.toCString(arena, serviceName), weight);
            if (rc != 0)
                throw new RuntimeException("zlink_provider_update_weight failed");
        }
    }

    public void unregister(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerUnregister(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw new RuntimeException("zlink_provider_unregister failed");
        }
    }

    public void setSockOpt(int role, int option, byte[] value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(value.length);
            MemorySegment.copy(MemorySegment.ofArray(value), 0, buf, 0, value.length);
            int rc = Native.providerSetSockOpt(handle, role, option, buf, value.length);
            if (rc != 0)
                throw new RuntimeException("zlink_provider_setsockopt failed");
        }
    }

    public void setSockOpt(int role, int option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.providerSetSockOpt(handle, role, option, buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_provider_setsockopt failed");
        }
    }

    public ProviderResult registerResult(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment status = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment resolved = arena.allocate(256);
            MemorySegment error = arena.allocate(256);
            int rc = Native.providerRegisterResult(handle, NativeHelpers.toCString(arena, serviceName),
                status, resolved, error);
            if (rc != 0)
                throw new RuntimeException("zlink_provider_register_result failed");
            int st = status.get(ValueLayout.JAVA_INT, 0);
            String resolvedEp = NativeHelpers.fromCString(resolved, 256);
            String errMsg = NativeHelpers.fromCString(error, 256);
            return new ProviderResult(st, resolvedEp, errMsg);
        }
    }

    public void setTlsServer(String cert, String key) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerSetTlsServer(handle, NativeHelpers.toCString(arena, cert),
                NativeHelpers.toCString(arena, key));
            if (rc != 0)
                throw new RuntimeException("zlink_provider_set_tls_server failed");
        }
    }

    public Socket routerSocket() {
        MemorySegment sock = Native.providerRouter(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_provider_router failed");
        return Socket.adopt(sock, false);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.providerDestroy(handle);
        handle = MemorySegment.NULL;
    }

    public record ProviderResult(int status, String resolvedEndpoint, String errorMessage) {}
}
