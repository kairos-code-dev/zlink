/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.service.registry.ServiceKind;
import dev.kairoscode.zlink.service.registry.ServiceRole;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record RegistryServiceSummaryFilter(ServiceKind serviceKind,
                                           ServiceRole serviceRole,
                                           String serviceName) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          serviceKind == null ? 0 : serviceKind.getValue());
        segment.set(ValueLayout.JAVA_INT, 4,
          serviceRole == null ? 0 : serviceRole.getValue());
        if (serviceName != null && !serviceName.isEmpty()) {
            MemorySegment name = NativeHelpers.toCString(arena, serviceName);
            MemorySegment.copy(name, 0, segment, 8,
              Math.min(name.byteSize(), 256));
        }
        return segment;
    }
}
