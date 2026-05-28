/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.eventing.MonitorStatus;
import systems.zlink.contracts.sockets.SocketType;
import systems.zlink.runtime.nativeapi.EnumCodecs;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMonitorStatuses;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record SpotNodeSocketEntry(
    SpotNodeSocketOwner owner,
    long ownerId,
    String ownerName,
    String socketName,
    SocketType socketType,
    boolean autoHwmVisible,
    MonitorStatus monitorStatus) {
    static SpotNodeSocketEntry fromNative(MemorySegment segment) {
        return new SpotNodeSocketEntry(
          EnumCodecs.spotNodeSocketOwnerFromValue(segment.get(ValueLayout.JAVA_INT, 0)),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED, 8),
          NativeHelpers.fromCString(segment.asSlice(16, 64), 64),
          NativeHelpers.fromCString(segment.asSlice(80, 64), 64),
          EnumCodecs.socketTypeFromValue(segment.get(ValueLayout.JAVA_INT, 144)),
          segment.get(ValueLayout.JAVA_INT, 148) != 0,
          NativeMonitorStatuses.fromNative(segment.asSlice(152,
            NativeLayouts.MONITOR_SNAPSHOT_LAYOUT.byteSize())));
    }
}
