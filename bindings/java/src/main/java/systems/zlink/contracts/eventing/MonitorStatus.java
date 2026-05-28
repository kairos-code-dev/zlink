/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.sockets.AutoHwmRecalcReason;

public record MonitorStatus(MonitorSourceKind sourceKind, int stateFlags,
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

    public boolean isReady() {
        return (stateFlags & MONITOR_STATE_READY) != 0;
    }
}
