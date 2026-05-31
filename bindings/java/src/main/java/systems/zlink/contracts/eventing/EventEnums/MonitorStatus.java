/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.sockets.AutoHwmRecalcReason;

/**
 * A snapshot of a monitored socket's state and auto-high-water-mark telemetry.
 * @param sourceKind what kind of source is being monitored
 * @param stateFlags bitmask of current state flags
 * @param detailFlags bitmask of detail flags
 * @param sndPendingMsgs pending outbound message count
 * @param rcvPendingMsgs pending inbound message count
 * @param autoHwmEnabled whether auto-HWM is enabled
 * @param autoHwmProfile the current auto-HWM sizing profile
 * @param autoHwmRole the socket's auto-HWM role
 * @param autoHwmPolicyClass the auto-HWM policy class
 * @param autoHwmUnitBudgetBytes the per-message unit budget in bytes
 * @param autoHwmSizeCap the computed auto-HWM size cap
 * @param autoHwmSocketMessageSlots the socket's message slot capacity
 * @param autoHwmEffectiveMessageBytes the effective message size in bytes
 * @param autoHwmAppliedSndHwm the applied send high-water mark
 * @param autoHwmAppliedRcvHwm the applied receive high-water mark
 * @param autoHwmAppliedSndBuffer the applied send buffer size
 * @param autoHwmAppliedRcvBuffer the applied receive buffer size
 * @param autoHwmLastRecalcMs when the last recalculation occurred, in milliseconds
 * @param autoHwmLastRecalcReason what triggered the last recalculation
 * @param autoHwmSendBlockedRatioPpm the send-blocked ratio in parts per million
 * @param autoHwmDeferredSndHwm the deferred send high-water mark
 * @param autoHwmDeferredRcvHwm the deferred receive high-water mark
 */
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
