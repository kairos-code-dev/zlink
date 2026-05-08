/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.service.registry.ServiceEventSubjectKind;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeSubjectFilter(SpotRole role, String subject,
                                    ServiceEventSubjectKind subjectKind) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SUBJECT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          role == null ? 0 : EnumCodecs.spotRoleValue(role));
        if (subject != null && !subject.isEmpty()) {
            MemorySegment nativeSubject = NativeHelpers.toCString(arena, subject);
            MemorySegment.copy(nativeSubject, 0, segment, 4,
              Math.min(nativeSubject.byteSize(), 256));
        }
        segment.set(ValueLayout.JAVA_INT, 260,
            subjectKind == null ? 0
              : EnumCodecs.serviceEventSubjectKindValue(subjectKind));
        return segment;
    }
}
