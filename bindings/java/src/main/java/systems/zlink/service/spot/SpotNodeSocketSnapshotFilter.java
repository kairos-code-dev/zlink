/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.SocketType;
import systems.zlink.internal.EnumCodecs;
import systems.zlink.internal.NativeHelpers;
import systems.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeSocketSnapshotFilter(
    SpotNodeSocketOwner owner,
    SocketType socketType,
    String socketName) {
    MemorySegment toNative(Arena arena) {
        MemorySegment segment = arena.allocate(
          NativeLayouts.SPOT_NODE_SOCKET_SNAPSHOT_FILTER_LAYOUT);
        segment.set(ValueLayout.JAVA_INT, 0,
          owner == null ? 0 : EnumCodecs.spotNodeSocketOwnerValue(owner));
        segment.set(ValueLayout.JAVA_INT, 4,
          socketType == null ? 0 : EnumCodecs.socketTypeValue(socketType));
        if (socketName != null && !socketName.isEmpty()) {
            MemorySegment nativeName = NativeHelpers.toCString(arena, socketName);
            MemorySegment.copy(nativeName, 0, segment, 8,
              Math.min(nativeName.byteSize(), 64));
        }
        return segment;
    }
}
