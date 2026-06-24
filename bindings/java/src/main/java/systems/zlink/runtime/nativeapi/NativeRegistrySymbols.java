/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

final class NativeRegistrySymbols {
    private static final MethodHandle MH_REG_NEW = downcall("zlink_registry_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_BIND = downcall("zlink_registry_bind",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SET = downcall("zlink_registry_set",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_REG_GET = downcall("zlink_registry_get",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_ADD_PEER = downcall(
            "zlink_registry_add_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_DESTROY = downcall(
            "zlink_registry_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_STATUS_SNAPSHOT = downcall(
            "zlink_registry_status",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_SERVICE_SUMMARY_SNAPSHOT = downcall(
            "zlink_registry_service_summary",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_MEMBER_PEERS = downcall(
            "zlink_registry_member_peers",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_TOPOLOGY = downcall(
            "zlink_registry_topology",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_CLIENT_NEW = downcall(
            "zlink_registry_query_client_new",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_CLIENT_CONNECT = downcall(
            "zlink_registry_query_client_connect",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_SNAPSHOT = downcall(
            "zlink_registry_query_client_topology",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_REG_QUERY_DESTROY = downcall(
            "zlink_registry_query_client_destroy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private NativeRegistrySymbols() {
    }

    static MemorySegment registryNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_REG_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_new failed", t);
        }
    }

    static int registryBind(MemorySegment reg, MemorySegment pub,
                            MemorySegment router) {
        try {
            return (int) MH_REG_BIND.invokeExact(reg, pub, router);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_bind failed", t);
        }
    }

    static int registrySetOption(MemorySegment reg, int option, int value) {
        try {
            return (int) MH_REG_SET.invokeExact(reg, option, value);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_set failed", t);
        }
    }

    static int registryGetOption(MemorySegment reg, int option,
                                 MemorySegment valueOut,
                                 MemorySegment errorOut) {
        try {
            return (int) MH_REG_GET.invokeExact(reg, option, valueOut, errorOut);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_get failed", t);
        }
    }

    static int registryAddPeer(MemorySegment reg, MemorySegment peer) {
        try {
            return (int) MH_REG_ADD_PEER.invokeExact(reg, peer);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_add_peer failed", t);
        }
    }

    static int registryDestroy(MemorySegment regPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment p = arena.allocate(ValueLayout.ADDRESS);
            p.set(ValueLayout.ADDRESS, 0, regPtr);
            return (int) MH_REG_DESTROY.invokeExact(p);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_destroy failed", t);
        }
    }

    static int registryStatus(MemorySegment registry, MemorySegment out) {
        try {
            return (int) MH_REG_STATUS_SNAPSHOT.invokeExact(registry, out);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_status failed", t);
        }
    }

    static int registryServiceSummary(MemorySegment registry,
                                      MemorySegment filter,
                                      MemorySegment entries,
                                      MemorySegment count) {
        try {
            return (int) MH_REG_SERVICE_SUMMARY_SNAPSHOT.invokeExact(registry,
                filter, entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_registry_service_summary failed", t);
        }
    }

    static int registryMemberPeers(MemorySegment registry,
                                   MemorySegment channelName,
                                   MemorySegment entries,
                                   MemorySegment count) {
        try {
            return (int) MH_REG_MEMBER_PEERS.invokeExact(registry, channelName,
                entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_member_peers failed", t);
        }
    }

    static int registryTopology(MemorySegment registry, MemorySegment entries,
                                MemorySegment count) {
        try {
            return (int) MH_REG_TOPOLOGY.invokeExact(registry,
                MemorySegment.NULL, entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_topology failed", t);
        }
    }

    static int registryTopology(MemorySegment registry, MemorySegment filter,
                                MemorySegment entries, MemorySegment count) {
        try {
            return (int) MH_REG_TOPOLOGY.invokeExact(registry, filter,
                entries, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_registry_topology failed", t);
        }
    }

    static MemorySegment registryQueryClientNew(MemorySegment ctx) {
        try {
            return (MemorySegment) MH_REG_QUERY_CLIENT_NEW.invokeExact(ctx);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_registry_query_client_new failed", t);
        }
    }

    static int registryQueryClientConnect(MemorySegment client,
                                          MemorySegment endpoint) {
        try {
            return (int) MH_REG_QUERY_CLIENT_CONNECT.invokeExact(client,
                endpoint);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_registry_query_client_connect failed", t);
        }
    }

    static int registryQuerySnapshot(MemorySegment client, MemorySegment filter,
                                     MemorySegment entries,
                                     MemorySegment count) {
        try {
            return (int) MH_REG_QUERY_SNAPSHOT.invokeExact(client, filter,
                entries, count);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_registry_query_client_topology failed", t);
        }
    }

    static int registryQueryDestroy(MemorySegment clientPtr) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment holder = arena.allocate(ValueLayout.ADDRESS);
            holder.set(ValueLayout.ADDRESS, 0, clientPtr);
            return (int) MH_REG_QUERY_DESTROY.invokeExact(holder);
        } catch (Throwable t) {
            throw new RuntimeException(
                "zlink_registry_query_client_destroy failed", t);
        }
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return NativeSymbols.downcall(name, fd);
    }
}
