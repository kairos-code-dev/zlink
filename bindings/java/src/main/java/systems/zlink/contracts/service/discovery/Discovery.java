/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.discovery;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.MemberPeerEntry;
import systems.zlink.contracts.ZlinkException;
import systems.zlink.runtime.nativebridge.InternalAccess;
import systems.zlink.runtime.nativebridge.ActorInterop;
import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import systems.zlink.runtime.nativebridge.NativeMsg;
import systems.zlink.runtime.nativebridge.ServiceDecoders;
import systems.zlink.contracts.service.spot.ActorRoute;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Fixed-channel discovery view.
 *
 * <p>One instance tracks exactly one {@link AutoConnectType}/{@code channelName}
 * pair and exposes spot owner resolution plus member peer snapshots for that view.
 */
public final class Discovery implements AutoCloseable {
    private static final int OPT_DISCOVERY_SPOT_OWNER_SYNC = 0x3035;
    private static final int OPT_DISCOVERY_ACTOR_ROUTE_SYNC = 0x3036;

    private MemorySegment handle;

    /** Opens a discovery handle for one auto-connect type and channel name. */
    public Discovery(Context ctx, AutoConnectType autoConnectType,
                     String channelName) {
        Objects.requireNonNull(ctx, "ctx");
        Objects.requireNonNull(autoConnectType, "autoConnectType");
        Objects.requireNonNull(channelName, "channelName");
        try (Arena arena = Arena.ofConfined()) {
            this.handle = Native.discoveryNewFixed(
              InternalAccess.contextHandle(ctx),
              EnumCodecs.autoConnectTypeValue(autoConnectType),
              NativeHelpers.toCString(arena, channelName));
        }
        if (handle == null || handle.address() == 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_discovery_new");
    }

    /** Returns the native discovery handle. */
    MemorySegment handle() {
        return handle;
    }

    /** Internal bridge for binding helpers. */
    MemorySegment handleInternal() {
        return handle();
    }

    /** Connects the discovery view to a registry router endpoint. */
    public void connectRegistry(String registryEndpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.discoveryConnectRegistry(handle,
              NativeHelpers.toCString(arena, registryEndpoint));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_connect_registry");
            }
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
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_resolve_spot");
            }
            return readRoutingId(ownerNodeRidOut);
        }
    }

    /** Resolves the current active route for one Actor id. */
    public ActorRoute resolveActor(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_ROUTE_LAYOUT);
            int rc = Native.discoveryResolveActor(handle,
              NativeHelpers.toCString(arena, actorId), out);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_resolve_actor");
            }
            return ActorInterop.actorRouteFromNative(out);
        }
    }

    /** Sets the service-local discovery value. */
    public void setValue(long value) {
        int rc = Native.discoverySetValue(handle, value);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_discovery_set_value");
    }

    /** Returns the current service-local discovery value. */
    public long getValue() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.discoveryGetValue(handle, out);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_discovery_get_value");
            return out.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    /** Enables or disables publishing spot owner records to Registry. */
    public void setSpotOwnerSyncEnabled(boolean enabled) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(ValueLayout.JAVA_INT);
            value.set(ValueLayout.JAVA_INT, 0, enabled ? 1 : 0);
            int rc = Native.setSockOpt(handle, OPT_DISCOVERY_SPOT_OWNER_SYNC, value,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_set_option");
        }
    }

    /** Returns whether this Discovery publishes spot owner records to Registry. */
    public boolean isSpotOwnerSyncEnabled() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSockOpt(handle, OPT_DISCOVERY_SPOT_OWNER_SYNC,
              value, len);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_get_option");
            return value.get(ValueLayout.JAVA_INT, 0) != 0;
        }
    }

    /** Enables or disables publishing actor active routes to Registry. */
    public void setActorRouteSyncEnabled(boolean enabled) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(ValueLayout.JAVA_INT);
            value.set(ValueLayout.JAVA_INT, 0, enabled ? 1 : 0);
            int rc = Native.setSockOpt(handle, OPT_DISCOVERY_ACTOR_ROUTE_SYNC, value,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_set_option");
        }
    }

    /** Returns whether this Discovery publishes actor active routes to Registry. */
    public boolean isActorRouteSyncEnabled() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSockOpt(handle, OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
              value, len);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_get_option");
            return value.get(ValueLayout.JAVA_INT, 0) != 0;
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
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_set_tls_client");
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
                throw InternalAccess.zlinkExceptionFromLastError("zlink_discovery_member_peers");
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
                throw InternalAccess.zlinkExceptionFromLastError("zlink_discovery_member_peers");
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
