/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryServiceSummaryFilter(AutoConnectType autoConnectType,
                                           ServiceRole serviceRole,
                                           String channelName) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          autoConnectType == null ? 0
            : EnumCodecs.autoConnectTypeValue(autoConnectType));
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceRole == null ? 0 : EnumCodecs.serviceRoleValue(serviceRole));
        if (channelName != null && !channelName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, channelName);
            MemorySegment.copy(name, 0, segment, 8,
              Math.min(name.byteSize(), 256));
        }
        return segment;
    }
}
