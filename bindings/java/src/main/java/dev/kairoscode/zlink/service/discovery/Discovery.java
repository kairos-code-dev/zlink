/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.discovery;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.service.registry.MemberPeerEntry;
import dev.kairoscode.zlink.service.registry.ServiceRole;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.internal.ServiceDecoders;
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
 * pair and exposes discovery metadata plus member peer snapshots for that view.
 */
public final class Discovery implements AutoCloseable {
    private MemorySegment handle;

    /** Opens a discovery handle for one service type and name. */
    public Discovery(Context ctx, ServiceType serviceType, String serviceName) {
        Objects.requireNonNull(ctx, "ctx");
        Objects.requireNonNull(serviceType, "serviceType");
        Objects.requireNonNull(serviceName, "serviceName");
        try (Arena arena = Arena.ofConfined()) {
            this.handle = Native.discoveryNewFixed(
              InternalAccess.contextHandle(ctx),
              serviceType.getValue(),
              NativeHelpers.toCString(arena, serviceName));
        }
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_discovery_new");
    }

    /** Returns the native discovery handle. */
    MemorySegment handle() {
        return handle;
    }

    /** Internal bridge for binding helpers. */
    public MemorySegment handleInternal() {
        return handle();
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

    /** Configures the DEALER auto-connect target policy for this view. */
    public void setDealerPeerMode(DiscoveryDealerPeerMode mode) {
        Objects.requireNonNull(mode, "mode");
        int rc = Native.discoverySetDealerPeerMode(handle, mode.getValue());
        if (rc != 0) {
            throw ZlinkException.fromLastError(
              "zlink_discovery_set_dealer_peer_mode");
        }
    }

    /** Resolves the current owner node routing id for one logical spot id. */
    public RoutingId resolveSpot(RoutingId spotRid) {
        Objects.requireNonNull(spotRid, "spotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeSpotRid = nativeRoutingId(arena, spotRid);
            MemorySegment ownerNodeRidOut = arena.allocate(
              NativeLayouts.ROUTING_ID_LAYOUT);
            int rc = Native.discoveryResolveSpot(handle, nativeSpotRid,
              ownerNodeRidOut);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_discovery_resolve_spot");
            }
            return readRoutingId(ownerNodeRidOut);
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

    /** Configures client TLS credentials for the discovery registry link. */
    public void setTlsClient(String caCertPem, String hostname,
                             boolean trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.setTlsClient(handle,
              NativeHelpers.toCString(arena, caCertPem),
              NativeHelpers.toCString(arena, hostname),
              trustSystem ? 1 : 0);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_set_tls_client");
            }
        }
    }

    /** Returns the current service-local metadata blob. */
    public byte[] getMetadata() {
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
                out.add(ServiceDecoders.memberPeerEntry(entries.asSlice(
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

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId rid) {
        byte[] value = InternalAccess.routingIdTrustedBytes(rid);
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
          (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private static RoutingId readRoutingId(MemorySegment nativeRid) {
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        byte[] value = new byte[size];
        if (size > 0) {
            MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
              MemorySegment.ofArray(value), 0, size);
        }
        return InternalAccess.routingIdFromTrusted(value);
    }

}
