// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed partial class MonitorStatus
{
    internal MonitorStatus(MonitorSourceKind sourceKind, uint stateFlags,
        uint detailFlags, ulong sndPendingMsgs, ulong rcvPendingMsgs,
        uint autoHwmEnabled, uint autoHwmProfile, uint autoHwmRole,
        uint autoHwmPolicyClass,
        ulong autoHwmUnitBudgetBytes, uint autoHwmSizeCap,
        ulong autoHwmSocketMessageSlots, ulong autoHwmEffectiveMessageBytes,
        int autoHwmAppliedSndHwm,
        int autoHwmAppliedRcvHwm,
        int autoHwmEffectiveSndbuf, int autoHwmEffectiveRcvbuf,
        ulong autoHwmLastRecalcMs,
        uint autoHwmLastRecalcReason, uint autoHwmSendBlockedRatioPpm,
        int autoHwmDeferredSndHwm, int autoHwmDeferredRcvHwm)
    {
        SourceKind = sourceKind;
        StateFlags = stateFlags;
        DetailFlags = detailFlags;
        SndPendingMsgs = sndPendingMsgs;
        RcvPendingMsgs = rcvPendingMsgs;
        AutoHwmEnabled = autoHwmEnabled != 0;
        AutoHwmProfile = autoHwmProfile;
        AutoHwmRole = autoHwmRole;
        AutoHwmPolicyClass = autoHwmPolicyClass;
        AutoHwmUnitBudgetBytes = autoHwmUnitBudgetBytes;
        AutoHwmSizeCap = autoHwmSizeCap;
        AutoHwmSocketMessageSlots = autoHwmSocketMessageSlots;
        AutoHwmEffectiveMessageBytes = autoHwmEffectiveMessageBytes;
        AutoHwmAppliedSndHwm = autoHwmAppliedSndHwm;
        AutoHwmAppliedRcvHwm = autoHwmAppliedRcvHwm;
        AutoHwmEffectiveSndbuf = autoHwmEffectiveSndbuf;
        AutoHwmEffectiveRcvbuf = autoHwmEffectiveRcvbuf;
        AutoHwmLastRecalcMs = autoHwmLastRecalcMs;
        AutoHwmLastRecalcReason = autoHwmLastRecalcReason;
        AutoHwmSendBlockedRatioPpm = autoHwmSendBlockedRatioPpm;
        AutoHwmDeferredSndHwm = autoHwmDeferredSndHwm;
        AutoHwmDeferredRcvHwm = autoHwmDeferredRcvHwm;
    }
}
