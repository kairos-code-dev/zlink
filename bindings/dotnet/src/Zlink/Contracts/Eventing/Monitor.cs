// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface ISocketMonitor : IDisposable, IAsyncDisposable
{
    void OnEvent(Action<MonitorEvent> handler);

    MonitorEvent? Recv(RecvFlags flags = RecvFlags.None);

    MonitorStatus Status();

    void Close();
}

public sealed record MonitorEvent(
    MonitorEventType Event,
    uint Value,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr);

public sealed class MonitorStatus
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

    public MonitorSourceKind SourceKind { get; }
    public uint StateFlags { get; }
    public uint DetailFlags { get; }
    public ulong SndPendingMsgs { get; }
    public ulong RcvPendingMsgs { get; }
    public bool AutoHwmEnabled { get; }
    public uint AutoHwmProfile { get; }
    public uint AutoHwmRole { get; }
    public uint AutoHwmPolicyClass { get; }
    public ulong AutoHwmUnitBudgetBytes { get; }
    public uint AutoHwmSizeCap { get; }
    public ulong AutoHwmSocketMessageSlots { get; }
    public ulong AutoHwmEffectiveMessageBytes { get; }
    public int AutoHwmAppliedSndHwm { get; }
    public int AutoHwmAppliedRcvHwm { get; }
    public int AutoHwmEffectiveSndbuf { get; }
    public int AutoHwmEffectiveRcvbuf { get; }
    public ulong AutoHwmLastRecalcMs { get; }
    public uint AutoHwmLastRecalcReason { get; }
    public uint AutoHwmSendBlockedRatioPpm { get; }
    public int AutoHwmDeferredSndHwm { get; }
    public int AutoHwmDeferredRcvHwm { get; }
    public bool IsReady => SourceKind == MonitorSourceKind.Socket
        && (StateFlags & 0x1u) != 0;

}
