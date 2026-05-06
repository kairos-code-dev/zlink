/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

import dev.kairoscode.zlink.AutoHwmProfile;
import dev.kairoscode.zlink.AutoHwmRecalcReason;
import dev.kairoscode.zlink.MonitorSnapshot;
import dev.kairoscode.zlink.MonitorSourceKind;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class NativeMonitorSnapshots {
    private NativeMonitorSnapshots() {
    }

    public static MonitorSnapshot fromNative(MemorySegment segment) {
        return new MonitorSnapshot(
          MonitorSourceKind.fromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_SOURCE_KIND_OFFSET)),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_STATE_FLAGS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_DETAIL_FLAGS_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_SND_PENDING_MSGS_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_RCV_PENDING_MSGS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_ENABLED_OFFSET) != 0,
          AutoHwmProfile.fromValue(segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_PROFILE_OFFSET)),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_ROLE_OFFSET),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_POLICY_CLASS_OFFSET),
	          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_UNIT_BUDGET_BYTES_OFFSET),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SIZE_CAP_OFFSET),
	          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SOCKET_MESSAGE_SLOTS_OFFSET),
	          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_MESSAGE_BYTES_OFFSET),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_APPLIED_SNDHWM_OFFSET),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_APPLIED_RCVHWM_OFFSET),
	          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_LAST_RECALC_MS_OFFSET),
	          AutoHwmRecalcReason.fromValue(segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_LAST_RECALC_REASON_OFFSET)),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SEND_BLOCKED_RATIO_PPM_OFFSET),
	          segment.get(ValueLayout.JAVA_INT,
	            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_DEFERRED_SNDHWM_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_DEFERRED_RCVHWM_OFFSET));
    }
}
