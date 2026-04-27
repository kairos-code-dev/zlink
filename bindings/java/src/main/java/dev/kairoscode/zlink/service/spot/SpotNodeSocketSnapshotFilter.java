/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeSocketSnapshotFilter(
    SpotNodeSocketOwner owner,
    SpotNodeSocketType socketType,
    String socketName) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          owner == null ? SpotNodeSocketOwner.ANY.getValue() : owner.getValue());
        segment.set(ValueLayout.JAVA_INT, 4,
          socketType == null ? SpotNodeSocketType.ANY.getValue()
                             : socketType.getValue());
        if (socketName != null && !socketName.isEmpty()) {
            MemorySegment nativeName = NativeHelpers.toCString(arena, socketName);
            MemorySegment.copy(nativeName, 0, segment, 8,
              Math.min(nativeName.byteSize(), 64));
        }
        return segment;
    }
}
