/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.discovery;

import systems.zlink.contracts.service.discovery.*;

import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorRoute;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.registry.MemberPeerEntry;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeListSnapshots;
import systems.zlink.runtime.service.registry.NativeRegistryCodecs;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

/**
 * Fixed-channel discovery view.
 *
 * <p>One instance tracks exactly one {@link AutoConnectType}/{@code channelName}
 * pair and exposes spot owner resolution plus member peer snapshots for that view.
 */
public final class NativeDiscovery implements Discovery {
    private static final int OPT_DISCOVERY_SPOT_OWNER_SYNC = 0x3035;
    private static final int OPT_DISCOVERY_ACTOR_ROUTE_SYNC = 0x3036;
    private static final int OPT_ROUTE_VALUE_MAX_SIZE = 0x3032;

    private MemorySegment handle;

    static {
        InternalAccess.register((InternalAccess.DiscoveryAccess) discovery -> ((NativeDiscovery) discovery).handle());
    }

    public static Discovery create(Context ctx, AutoConnectType autoConnectType,
                                   String channelName) {
        return new NativeDiscovery(ctx, autoConnectType, channelName);
    }

    /** Opens a discovery handle for one auto-connect type and channel name. */
    NativeDiscovery(Context ctx, AutoConnectType autoConnectType,
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

    public int routeValueMaxSize() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment value = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_LONG.byteSize());
            int rc = Native.getSockOpt(handle, OPT_ROUTE_VALUE_MAX_SIZE,
              value, len);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_get_option");
            }
            long result = value.get(ValueLayout.JAVA_LONG, 0);
            if (result > Integer.MAX_VALUE) {
                throw new IllegalStateException(
                  "routeValueMaxSize exceeds int range: " + result);
            }
            return (int) result;
        }
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

    /** Resolves the current owner node and Spot kind for one logical spot id. */
    public SpotRoute resolveSpot(RoutingId spotRid) {
        Objects.requireNonNull(spotRid, "spotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeSpotRid = nativeRoutingId(arena, spotRid);
            MemorySegment routeOut = arena.allocate(
              NativeLayouts.SPOT_ROUTE_LAYOUT);
            int rc = Native.discoveryResolveSpot(handle, nativeSpotRid,
              routeOut);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_resolve_spot");
            }
            return new SpotRoute(
              readRoutingId(routeOut.asSlice(
                NativeLayouts.SPOT_ROUTE_SPOT_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
              readRoutingId(routeOut.asSlice(
                NativeLayouts.SPOT_ROUTE_OWNER_NODE_RID_OFFSET,
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
              EnumCodecs.spotKindFromValue(routeOut.get(ValueLayout.JAVA_INT,
                NativeLayouts.SPOT_ROUTE_SPOT_KIND_OFFSET)));
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

    public void bindRoute(int kind, byte[] key, byte[] value) {
        Objects.requireNonNull(key, "key");
        Objects.requireNonNull(value, "value");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeKey = copyBytes(arena, key);
            MemorySegment nativeValue = copyBytes(arena, value);
            int rc = Native.discoveryBindRoute(handle, kind, nativeKey,
              key.length, nativeValue, value.length);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_bind_route");
            }
        }
    }

    public void unbindRoute(int kind, byte[] key) {
        Objects.requireNonNull(key, "key");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeKey = copyBytes(arena, key);
            int rc = Native.discoveryUnbindRoute(handle, kind, nativeKey,
              key.length);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_unbind_route");
            }
        }
    }

    public DiscoveryRoute resolveRoute(int kind, byte[] key) {
        Objects.requireNonNull(key, "key");
        Message value = new Message();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeKey = copyBytes(arena, key);
            MemorySegment ownerRidOut = arena.allocate(
              NativeLayouts.ROUTING_ID_LAYOUT);
            int rc = Native.discoveryResolveRoute(handle, kind, nativeKey,
              key.length, ownerRidOut, InternalAccess.messageNativeHandle(value));
            if (rc != 0) {
                value.close();
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_discovery_resolve_route");
            }
            return new DiscoveryRoute(readRoutingId(ownerRidOut), value);
        } catch (RuntimeException ex) {
            value.close();
            throw ex;
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
        return NativeListSnapshots.read(
          NativeLayouts.MEMBER_PEER_ENTRY_LAYOUT,
          "zlink_discovery_member_peers",
          (arena, entries, count) -> Native.discoveryMemberPeers(handle,
            entries, count),
          NativeRegistryCodecs::memberPeerEntryFromNative);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.discoveryDestroy(handle);
        handle = MemorySegment.NULL;
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

    private static MemorySegment copyBytes(Arena arena, byte[] bytes) {
        MemorySegment out = arena.allocate(bytes.length);
        if (bytes.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(Arrays.copyOf(bytes,
              bytes.length)), 0, out, 0, bytes.length);
        }
        return out;
    }

}
