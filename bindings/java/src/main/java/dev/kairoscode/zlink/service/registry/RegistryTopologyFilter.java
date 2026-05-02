/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryTopologyFilter(AutoConnectType autoConnectType,
                                     ServiceKind serviceKind,
                                     ServiceRole serviceRole,
                                     String channelName, RoutingId routingId,
                                     TopologyState state,
                                     TopologySource source) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          autoConnectType == null ? 0 : autoConnectType.getValue());
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceKind == null ? 0 : serviceKind.getValue());
        segment.set(ValueLayout.JAVA_INT, 8,
          serviceRole == null ? 0 : serviceRole.getValue());
        if (channelName != null && !channelName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment.copy(name, 0, segment, 12,
              Math.min(name.byteSize(), 256));
        }
        if (routingId != null) {
            byte[] bytes = InternalAccess.routingIdTrustedBytes(routingId);
            segment.set(ValueLayout.JAVA_BYTE, 268, (byte) bytes.length);
            if (bytes.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(bytes), 0, segment,
                  269, bytes.length);
            }
        }
        segment.set(ValueLayout.JAVA_INT, 524, state == null ? 0 : state.getValue());
        segment.set(ValueLayout.JAVA_INT, 528, source == null ? 0 : source.getValue());
        return segment;
    }
}
