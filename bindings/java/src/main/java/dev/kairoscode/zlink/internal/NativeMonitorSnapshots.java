/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.internal;

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
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_ROLE_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_MANAGED_CONNECTIONS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_ACTIVE_HWM_CONNECTIONS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_OBSERVED_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_PLANNING_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_CONTEXT_TOTAL_PLANNING_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_BASE_FLOOR_PER_CONNECTION_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_APPLIED_SNDHWM_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_APPLIED_RCVHWM_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_REQUESTED_SNDBUF_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_REQUESTED_RCVBUF_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_SNDBUF_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_RCVBUF_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_TOTAL_MEMORY_BUDGET_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_QUEUE_BUDGET_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_TRANSPORT_BUDGET_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_RUNTIME_RESERVE_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SOCKET_QUEUE_SHARE_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SOCKET_MESSAGE_SLOTS_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_EFFECTIVE_MESSAGE_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_ESTIMATED_MAX_MEMORY_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_LAST_RECALC_MS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_LAST_RECALC_REASON_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SEND_BLOCKED_RATIO_PPM_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SCOPE_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_SCOPE_COUNT_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_AUTO_BUFFER_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_MANUAL_BUFFER_BYTES_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_BUFFER_CONNECTIONS_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_DEFERRED_SNDHWM_OFFSET),
          segment.get(ValueLayout.JAVA_INT,
            NativeLayouts.MONITOR_SNAPSHOT_AUTO_HWM_DEFERRED_RCVHWM_OFFSET));
    }
}
