/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.service.registry.ServiceKind;
import dev.kairoscode.zlink.service.registry.ServiceRole;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryTopologyFilter(ServiceKind serviceKind,
                                     ServiceRole serviceRole,
                                     String serviceName, RoutingId routingId,
                                     TopologyState state,
                                     TopologySource source) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_TOPOLOGY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          serviceKind == null ? 0 : serviceKind.getValue());
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceRole == null ? 0 : serviceRole.getValue());
        if (serviceName != null && !serviceName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, serviceName);
            MemorySegment.copy(name, 0, segment, 8,
              Math.min(name.byteSize(), 256));
        }
        if (routingId != null) {
            byte[] bytes = routingId.toBytes();
            segment.set(ValueLayout.JAVA_BYTE, 264, (byte) bytes.length);
            if (bytes.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(bytes), 0, segment,
                  265, bytes.length);
            }
        }
        segment.set(ValueLayout.JAVA_INT, 520, state == null ? 0 : state.getValue());
        segment.set(ValueLayout.JAVA_INT, 524, source == null ? 0 : source.getValue());
        return segment;
    }
}
