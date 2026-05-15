/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.MonitorSnapshot;
import systems.zlink.contracts.SocketType;
import systems.zlink.runtime.nativebridge.EnumCodecs;
import systems.zlink.runtime.nativebridge.NativeHelpers;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import systems.zlink.runtime.nativebridge.NativeMonitorSnapshots;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeSocketSnapshotEntry(
    SpotNodeSocketOwner owner,
    long ownerId,
    String ownerName,
    String socketName,
    SocketType socketType,
    boolean autoHwmVisible,
    MonitorSnapshot snapshot) {
    static SpotNodeSocketSnapshotEntry fromNative(MemorySegment segment) {
        return new SpotNodeSocketSnapshotEntry(
          EnumCodecs.spotNodeSocketOwnerFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 8),
          NativeHelpers.fromCString(segment.asSlice(16, 64), 64),
          NativeHelpers.fromCString(segment.asSlice(80, 64), 64),
          EnumCodecs.socketTypeFromValue(segment.get(ValueLayout.JAVA_INT, 144)),
          segment.get(ValueLayout.JAVA_INT, 148) != 0,
          NativeMonitorSnapshots.fromNative(segment.asSlice(152,
            NativeLayouts.MONITOR_SNAPSHOT_LAYOUT.byteSize())));
    }
}
