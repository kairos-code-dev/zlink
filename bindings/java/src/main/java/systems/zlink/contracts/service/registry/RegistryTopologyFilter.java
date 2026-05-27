/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
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
          autoConnectType == null ? 0
            : EnumCodecs.autoConnectTypeValue(autoConnectType));
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceKind == null ? 0 : EnumCodecs.serviceKindValue(serviceKind));
        segment.set(ValueLayout.JAVA_INT, 8,
          serviceRole == null ? 0 : EnumCodecs.serviceRoleValue(serviceRole));
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
        segment.set(ValueLayout.JAVA_INT, 524,
          state == null ? 0 : EnumCodecs.topologyStateValue(state));
        segment.set(ValueLayout.JAVA_INT, 528,
          source == null ? 0 : EnumCodecs.topologySourceValue(source));
        return segment;
    }
}
