// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class MonitorConverters
{
    internal static MonitorEvent FromNative(ref ZlinkMonitorEvent evt)
    {
        string local;
        string remote;
        unsafe
        {
            fixed (byte* localPtr = evt.LocalAddr)
            fixed (byte* remotePtr = evt.RemoteAddr)
            {
                local = NativeHelpers.ReadString(localPtr, 256);
                remote = NativeHelpers.ReadString(remotePtr, 256);
            }
        }

        return new MonitorEvent((MonitorEventType)(evt.Event & 0xFFFFFFFFuL),
            (uint)evt.Value, RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref evt.RoutingId)),
            local, remote);
    }

    internal static MonitorStatus FromNative(ref ZlinkMonitorStatus native)
    {
        return new MonitorStatus((MonitorSourceKind)native.MonitorSourceKind,
            native.StateFlags, native.DetailFlags, native.SndPendingMsgs,
            native.RcvPendingMsgs, native.AutoHwmEnabled,
            native.AutoHwmProfile, native.AutoHwmRole,
            native.AutoHwmPolicyClass,
            native.AutoHwmUnitBudgetBytes,
            native.AutoHwmSizeCap,
            native.AutoHwmSocketMessageSlots,
            native.AutoHwmEffectiveMessageBytes,
            native.AutoHwmAppliedSndHwm, native.AutoHwmAppliedRcvHwm,
            native.AutoHwmEffectiveSndbuf, native.AutoHwmEffectiveRcvbuf,
            native.AutoHwmLastRecalcMs,
            native.AutoHwmLastRecalcReason,
            native.AutoHwmSendBlockedRatioPpm,
            native.AutoHwmDeferredSndHwm,
            native.AutoHwmDeferredRcvHwm);
    }
}
