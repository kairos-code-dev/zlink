/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeMonitorSnapshots;
import java.lang.foreign.MemorySegment;

public record MonitorSnapshot(MonitorSourceKind sourceKind, int stateFlags,
                              int detailFlags,
                              long sndPendingMsgs, long rcvPendingMsgs,
                              boolean autoHwmEnabled, int autoHwmProfile,
                              int autoHwmRole, int autoHwmPolicyClass,
                              int autoHwmManagedConnections,
                              int autoHwmActiveHwmConnections,
                              int autoHwmObservedCount,
                              int autoHwmPlanningCount,
                              int autoHwmContextTotalPlanningCount,
                              int autoHwmBaseFloorPerConnection,
                              long autoHwmUnitBudgetBytes,
                              int autoHwmSizeCap,
                              int autoHwmEffectivePublishFanout,
                              int autoHwmAppliedSndHwm,
                              int autoHwmAppliedRcvHwm,
                              int autoHwmRequestedSndBuf,
                              int autoHwmRequestedRcvBuf,
                              int autoHwmEffectiveSndBuf,
                              int autoHwmEffectiveRcvBuf,
                              long autoHwmTotalMemoryBudgetBytes,
                              long autoHwmQueueBudgetBytes,
                              long autoHwmTransportBudgetBytes,
                              long autoHwmRuntimeReserveBytes,
                              long autoHwmSocketQueueShareBytes,
                              long autoHwmSocketMessageSlots,
                              long autoHwmEffectiveMessageBytes,
                              long autoHwmEstimatedMaxMemoryBytes,
                              long autoHwmLastRecalcMs,
                              int autoHwmLastRecalcReason,
                              int autoHwmSendBlockedRatioPpm,
                              int autoHwmScope, int autoHwmScopeCount,
                              long autoHwmAutoBufferBytes,
                              long autoHwmManualBufferBytes,
                              int autoHwmBufferConnections,
                              int autoHwmDeferredSndHwm,
                              int autoHwmDeferredRcvHwm) {
    private static final int MONITOR_STATE_READY = 1 << 0;

    static MonitorSnapshot fromNative(MemorySegment segment) {
        return NativeMonitorSnapshots.fromNative(segment);
    }

    public boolean isReady() {
        return (stateFlags & MONITOR_STATE_READY) != 0;
    }
}
