/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.discovery;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MemberPeerEntry;
import dev.kairoscode.zlink.ServiceRole;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Fixed-service discovery view.
 *
 * <p>One instance tracks exactly one {@link ServiceType}/{@code serviceName}
 * pair and exposes discovery metadata, member peer snapshots, and
 * service-monitor access for that view.
 */
public final class Discovery implements AutoCloseable {
    private final String serviceName;
    private final ServiceType serviceType;
    private MemorySegment handle;

    /** Opens a discovery handle for one service type and name. */
    public Discovery(Context ctx, ServiceType serviceType, String serviceName) {
        Objects.requireNonNull(ctx, "ctx");
        this.serviceType = Objects.requireNonNull(serviceType, "serviceType");
        this.serviceName = Objects.requireNonNull(serviceName, "serviceName");
        try (Arena arena = Arena.ofConfined()) {
            this.handle = Native.discoveryNewFixed(ctx.handle(),
              (short) serviceType.getValue(),
              NativeHelpers.toCString(arena, serviceName));
        }
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_discovery_new");
    }

    /** Returns the native discovery handle. */
    public MemorySegment handle() {
        return handle;
    }

    /** Returns the fixed service type of this discovery view. */
    public ServiceType serviceType() {
        return serviceType;
    }

    /** Returns the fixed service name of this discovery view. */
    public String serviceName() {
        return serviceName;
    }

    /** Connects the discovery view to a registry router endpoint. */
    public void connectRegistry(String registryEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryConnectRegistry(handle,
              NativeHelpers.toCString(arena, registryEndpoint));
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_discovery_connect_registry");
            }
        }
    }

    /** Sets the service-local discovery value. */
    public void setValue(long value) {
        int rc = Native.discoverySetValue(handle, value);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_discovery_set_value");
    }

    /** Returns the current service-local discovery value. */
    public long getValue() {
        return value();
    }

    public long value() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.discoveryGetValue(handle, out);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_get_value");
            return out.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    /** Sets the service-local metadata blob. */
    public void setMetadata(byte[] metadata) {
        Objects.requireNonNull(metadata, "metadata");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment data = metadata.length == 0
              ? MemorySegment.NULL
              : arena.allocate(metadata.length);
            if (metadata.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(metadata), 0, data, 0,
                  metadata.length);
            }
            int rc = Native.discoverySetMetadata(handle, data, metadata.length);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_discovery_set_metadata");
            }
        }
    }

    /** Returns the current service-local metadata blob. */
    public byte[] getMetadata() {
        return metadata();
    }

    public byte[] metadata() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment metadata = arena.allocate(NativeLayouts.MSG_LAYOUT);
            initMessage(metadata);
            try {
                int rc = Native.discoveryGetMetadata(handle, metadata);
                if (rc != 0) {
                    throw ZlinkException.fromLastError(
                      "zlink_discovery_get_metadata");
                }
                return readMessageBytes(metadata);
            } finally {
                NativeMsg.msgClose(metadata);
            }
        }
    }

    /** Opens a service monitor for this discovery handle. */
    public dev.kairoscode.zlink.ServiceMonitor monitorOpen(int events) {
        MemorySegment monitor = Native.serviceMonitorOpen(handle, events);
        if (monitor == null || monitor.address() == 0) {
            throw ZlinkException.fromLastError("zlink_service_monitor_open");
        }
        return new dev.kairoscode.zlink.ServiceMonitor(monitor);
    }

    /** Returns the current discovery member peers snapshot. */
    public List<MemberPeerEntry> memberPeers() {
        int count = memberPeerCount();
        if (count == 0)
            return List.of();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment entries = arena.allocate(
              NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT, count);
            MemorySegment countOut = arena.allocate(ValueLayout.JAVA_LONG);
            countOut.set(ValueLayout.JAVA_LONG, 0, count);
            int rc = Native.discoveryMemberPeers(handle, entries, countOut);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_member_peers");
            int actual = boundedCount(countOut.get(ValueLayout.JAVA_LONG, 0),
              count);
            long stride = NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT.byteSize();
            ArrayList<MemberPeerEntry> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(MemberPeerEntry.fromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    /** Returns the metadata blob for one discovered member peer. */
    public byte[] memberPeerMetadata(ServiceRole serviceRole, String endpoint) {
        Objects.requireNonNull(serviceRole, "serviceRole");
        Objects.requireNonNull(endpoint, "endpoint");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment metadata = arena.allocate(NativeLayouts.MSG_LAYOUT);
            initMessage(metadata);
            try {
            int rc = Native.discoveryMemberPeerMetadata(handle,
              (short) serviceRole.getValue(),
              NativeHelpers.toCString(arena, endpoint), metadata);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_discovery_member_peer_metadata");
            }
            return readMessageBytes(metadata);
            } finally {
                NativeMsg.msgClose(metadata);
            }
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.discoveryDestroy(handle);
        handle = MemorySegment.NULL;
    }

    private int memberPeerCount() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.discoveryMemberPeers(handle, MemorySegment.NULL,
              count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_discovery_member_peers");
            return boundedCount(count.get(ValueLayout.JAVA_LONG, 0),
              Integer.MAX_VALUE);
        }
    }

    private static int boundedCount(long value, int max) {
        if (value <= 0)
            return 0;
        if (value > max)
            return max;
        return (int) value;
    }

    private static void initMessage(MemorySegment message) {
        int rc = NativeMsg.msgInit(message);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init");
    }

    private static byte[] readMessageBytes(MemorySegment message) {
        int size = Math.toIntExact(NativeMsg.msgSize(message));
        byte[] bytes = new byte[size];
        if (size > 0) {
            MemorySegment.copy(NativeMsg.msgData(message).reinterpret(size), 0,
              MemorySegment.ofArray(bytes), 0, size);
        }
        return bytes;
    }
}
