/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

final class NativeDiscoverySymbols {
    private static final MethodHandle MH_DISC_NEW_FIXED = downcall(
            "zlink_discovery_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_CONNECT = downcall(
            "zlink_discovery_connect_registry",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_RESOLVE_SPOT = downcall(
            "zlink_discovery_resolve_spot",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_RESOLVE_ACTOR = downcall(
            "zlink_discovery_resolve_actor",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_BIND_ROUTE = downcall(
            "zlink_discovery_bind_route",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_DISC_UNBIND_ROUTE = downcall(
            "zlink_discovery_unbind_route",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_DISC_RESOLVE_ROUTE = downcall(
            "zlink_discovery_resolve_route",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_SET_VALUE = downcall(
            "zlink_discovery_set_value",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_DISC_GET_VALUE = downcall(
            "zlink_discovery_get_value",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_DESTROY = downcall(
            "zlink_discovery_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_DISC_MEMBER_PEERS = downcall(
            "zlink_discovery_member_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private NativeDiscoverySymbols() {
    }

    static MemorySegment discoveryNewFixed(MemorySegment ctx,
                                           int autoConnectType,
                                           MemorySegment channelName) {
        try {
            return (MemorySegment) MH_DISC_NEW_FIXED.invokeExact(ctx,
                autoConnectType, channelName);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_new failed", t);
        }
    }

    static int discoveryConnectRegistry(MemorySegment disc, MemorySegment pub) {
        try {
            return (int) MH_DISC_CONNECT.invokeExact(disc, pub);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_discovery_connect_registry failed", t);
        }
    }

    static int discoveryResolveSpot(MemorySegment discovery,
                                    MemorySegment spotRid,
                                    MemorySegment routeOut) {
        try {
            return (int) MH_DISC_RESOLVE_SPOT.invokeExact(discovery, spotRid,
                routeOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_resolve_spot failed",
                t);
        }
    }

    static int discoveryResolveActor(MemorySegment discovery,
                                     MemorySegment actorId,
                                     MemorySegment routeOut) {
        try {
            return (int) MH_DISC_RESOLVE_ACTOR.invokeExact(discovery, actorId,
                routeOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_resolve_actor failed",
                t);
        }
    }

    static int discoveryBindRoute(MemorySegment discovery, int kind,
                                  MemorySegment key, long keySize,
                                  MemorySegment value, long valueSize) {
        try {
            return (int) MH_DISC_BIND_ROUTE.invokeExact(discovery, kind, key,
                keySize, value, valueSize);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_bind_route failed", t);
        }
    }

    static int discoveryUnbindRoute(MemorySegment discovery, int kind,
                                    MemorySegment key, long keySize) {
        try {
            return (int) MH_DISC_UNBIND_ROUTE.invokeExact(discovery, kind, key,
                keySize);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_unbind_route failed",
                t);
        }
    }

    static int discoveryResolveRoute(MemorySegment discovery, int kind,
                                     MemorySegment key, long keySize,
                                     MemorySegment ownerRoutingIdOut,
                                     MemorySegment valueOut) {
        try {
            return (int) MH_DISC_RESOLVE_ROUTE.invokeExact(discovery, kind, key,
                keySize, ownerRoutingIdOut, valueOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_resolve_route failed",
                t);
        }
    }

    static int discoverySetValue(MemorySegment disc, long value) {
        try {
            return (int) MH_DISC_SET_VALUE.invokeExact(disc, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_set_value failed", t);
        }
    }

    static int discoveryGetValue(MemorySegment disc, MemorySegment valueOut) {
        try {
            return (int) MH_DISC_GET_VALUE.invokeExact(disc, valueOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_get_value failed", t);
        }
    }

    static int discoveryDestroy(MemorySegment discPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, discPtr);
            return (int) MH_DISC_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_destroy failed", t);
        }
    }

    static int discoveryMemberPeers(MemorySegment discovery,
                                    MemorySegment entries,
                                    MemorySegment count) {
        try {
            return (int) MH_DISC_MEMBER_PEERS.invokeExact(discovery, entries,
                count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_discovery_member_peers failed",
                t);
        }
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return NativeSymbols.downcall(name, fd);
    }
}
