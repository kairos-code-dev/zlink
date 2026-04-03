/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public record MonitorSnapshot(int sourceKind, int stateFlags, int detailFlags,
                              long sndPendingMsgs, long rcvPendingMsgs) {
    public static MonitorSnapshot fromNative(MemorySegment segment) {
        return new MonitorSnapshot(
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_SOURCE_KIND_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_STATE_FLAGS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_DETAIL_FLAGS_OFFSET),
          segment.get(ValueLayout.JAVA_LONG,
            NativeLayouts.MONITOR_SNAPSHOT_SND_PENDING_MSGS_OFFSET),
          segment.get(ValueLayout.JAVA_LONG,
            NativeLayouts.MONITOR_SNAPSHOT_RCV_PENDING_MSGS_OFFSET));
    }
}
