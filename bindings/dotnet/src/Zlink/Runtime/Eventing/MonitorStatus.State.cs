// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

public sealed partial class MonitorStatus
{
    internal MonitorStatus(in ZlinkMonitorStatus native)
    {
        SourceKind = (MonitorSourceKind)native.MonitorSourceKind;
        StateFlags = (MonitorStateFlags)native.StateFlags;
        DetailFlags = (MonitorStatusDetailFlags)native.DetailFlags;
        SndPendingMsgs = native.SndPendingMsgs;
        RcvPendingMsgs = native.RcvPendingMsgs;
        AutoHwmEnabled = native.AutoHwmEnabled != 0;
        AutoHwmProfile = (AutoHwmProfile)native.AutoHwmProfile;
        AutoHwmRole = native.AutoHwmRole;
        AutoHwmPolicyClass = native.AutoHwmPolicyClass;
        AutoHwmUnitBudgetBytes = native.AutoHwmUnitBudgetBytes;
        AutoHwmSizeCap = native.AutoHwmSizeCap;
        AutoHwmSocketMessageSlots = native.AutoHwmSocketMessageSlots;
        AutoHwmConnectionBucketEnabled =
            native.AutoHwmConnectionBucketEnabled != 0;
        AutoHwmConnectionBucketCount = native.AutoHwmConnectionBucketCount;
        AutoHwmConnectionBucketIndex = native.AutoHwmConnectionBucketIndex;
        AutoHwmConnectionBucketHwm4K = native.AutoHwmConnectionBucketHwm4K;
        AutoHwmConnectionBucketHysteresisRetained =
            native.AutoHwmConnectionBucketHysteresisRetained != 0;
        AutoHwmEffectiveMessageBytes = native.AutoHwmEffectiveMessageBytes;
        AutoHwmAppliedSndHwm = native.AutoHwmAppliedSndHwm;
        AutoHwmAppliedRcvHwm = native.AutoHwmAppliedRcvHwm;
        AutoHwmEffectiveSndbuf = native.AutoHwmEffectiveSndbuf;
        AutoHwmEffectiveRcvbuf = native.AutoHwmEffectiveRcvbuf;
        AutoHwmLastRecalcMs = native.AutoHwmLastRecalcMs;
        AutoHwmLastRecalcReason =
            (AutoHwmRecalcReason)native.AutoHwmLastRecalcReason;
        AutoHwmSendBlockedRatioPpm = native.AutoHwmSendBlockedRatioPpm;
        AutoHwmDeferredSndHwm = native.AutoHwmDeferredSndHwm;
        AutoHwmDeferredRcvHwm = native.AutoHwmDeferredRcvHwm;
    }
}
