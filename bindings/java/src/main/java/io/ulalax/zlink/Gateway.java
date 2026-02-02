package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import io.ulalax.zlink.internal.NativeLayouts;
import io.ulalax.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Gateway implements AutoCloseable {
    private MemorySegment handle;

    public Gateway(Context ctx, Discovery discovery) {
        this.handle = Native.gatewayNew(ctx.handle(), discovery.handle());
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_gateway_new failed");
    }

    public void send(String serviceName, Message[] parts, int flags) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
            for (int i = 0; i < parts.length; i++) {
                MemorySegment dest = vec.asSlice((long) i * NativeLayouts.MSG_LAYOUT.byteSize(),
                    NativeLayouts.MSG_LAYOUT.byteSize());
                NativeMsg.msgCopy(dest, parts[i].handle());
            }
            try {
                int rc = Native.gatewaySend(handle, NativeHelpers.toCString(arena, serviceName), vec, parts.length, flags);
                if (rc != 0)
                    throw new RuntimeException("zlink_gateway_send failed");
            } finally {
                for (int i = 0; i < parts.length; i++) {
                    MemorySegment msg = vec.asSlice((long) i * NativeLayouts.MSG_LAYOUT.byteSize(),
                        NativeLayouts.MSG_LAYOUT.byteSize());
                    NativeMsg.msgClose(msg);
                }
            }
        }
    }

    public GatewayMessage recv(int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment service = arena.allocate(256);
            int rc = Native.gatewayRecv(handle, partsPtr, count, flags, service);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment parts = partsPtr.get(ValueLayout.ADDRESS, 0);
            byte[][] data = NativeMsg.readMsgVector(parts, partCount);
            String serviceName = NativeHelpers.fromCString(service, 256);
            return new GatewayMessage(serviceName, data);
        }
    }

    public void setLoadBalancing(String serviceName, int strategy) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewaySetLbStrategy(handle, NativeHelpers.toCString(arena, serviceName), strategy);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_set_lb_strategy failed");
        }
    }

    public void setTlsClient(String caCert, String hostname, int trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewaySetTlsClient(handle, NativeHelpers.toCString(arena, caCert),
                NativeHelpers.toCString(arena, hostname), trustSystem);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_set_tls_client failed");
        }
    }

    public int connectionCount(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewayConnectionCount(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw new RuntimeException("zlink_gateway_connection_count failed");
            return rc;
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.gatewayDestroy(handle);
        handle = MemorySegment.NULL;
    }

    public record GatewayMessage(String serviceName, byte[][] parts) {}
}
