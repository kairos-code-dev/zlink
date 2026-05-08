/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeMonitorSnapshots;
import java.lang.foreign.MemorySegment;

public record MonitorSnapshot(MonitorSourceKind sourceKind, int stateFlags,
                              int detailFlags,
                              long sndPendingMsgs, long rcvPendingMsgs,
                              boolean autoHwmEnabled,
                              AutoHwmProfile autoHwmProfile,
                              int autoHwmRole, int autoHwmPolicyClass,
                              long autoHwmUnitBudgetBytes,
                              int autoHwmSizeCap,
                              long autoHwmSocketMessageSlots,
                              long autoHwmEffectiveMessageBytes,
                              int autoHwmAppliedSndHwm,
                              int autoHwmAppliedRcvHwm,
                              int autoHwmAppliedSndBuffer,
                              int autoHwmAppliedRcvBuffer,
                              long autoHwmLastRecalcMs,
                              AutoHwmRecalcReason autoHwmLastRecalcReason,
                              int autoHwmSendBlockedRatioPpm,
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
