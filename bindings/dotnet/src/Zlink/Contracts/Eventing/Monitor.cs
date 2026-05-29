// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the socket monitor contract.
/// </summary>
public interface ISocketMonitor : IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Registers a handler for event.
    /// </summary>
    void OnEvent(Action<MonitorEvent> handler);

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    MonitorEvent? Recv(RecvFlags flags = RecvFlags.None);

    /// <summary>
    /// Gets the current status.
    /// </summary>
    MonitorStatus Status();

    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}

/// <summary>
/// Describes monitor event data.
/// </summary>
/// <param name="Event">The event value.</param>
/// <param name="Value">The value value.</param>
/// <param name="RoutingId">The routing id value.</param>
/// <param name="LocalAddr">The local addr value.</param>
/// <param name="RemoteAddr">The remote addr value.</param>
public sealed record MonitorEvent(
    MonitorEventType Event,
    uint Value,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr);

/// <summary>
/// Represents monitor status.
/// </summary>
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

    /// <summary>
    /// Gets or sets the source kind.
    /// </summary>
    public MonitorSourceKind SourceKind { get; }
    /// <summary>
    /// Gets or sets the state flags.
    /// </summary>
    public uint StateFlags { get; }
    /// <summary>
    /// Gets or sets the detail flags.
    /// </summary>
    public uint DetailFlags { get; }
    /// <summary>
    /// Gets or sets the snd pending msgs.
    /// </summary>
    public ulong SndPendingMsgs { get; }
    /// <summary>
    /// Gets or sets the rcv pending msgs.
    /// </summary>
    public ulong RcvPendingMsgs { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark enabled.
    /// </summary>
    public bool AutoHwmEnabled { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark profile.
    /// </summary>
    public uint AutoHwmProfile { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark role.
    /// </summary>
    public uint AutoHwmRole { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark policy class.
    /// </summary>
    public uint AutoHwmPolicyClass { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark unit budget bytes.
    /// </summary>
    public ulong AutoHwmUnitBudgetBytes { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark size cap.
    /// </summary>
    public uint AutoHwmSizeCap { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark socket message slots.
    /// </summary>
    public ulong AutoHwmSocketMessageSlots { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark effective message bytes.
    /// </summary>
    public ulong AutoHwmEffectiveMessageBytes { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark applied snd hwm.
    /// </summary>
    public int AutoHwmAppliedSndHwm { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark applied rcv hwm.
    /// </summary>
    public int AutoHwmAppliedRcvHwm { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark effective sndbuf.
    /// </summary>
    public int AutoHwmEffectiveSndbuf { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark effective rcvbuf.
    /// </summary>
    public int AutoHwmEffectiveRcvbuf { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark last recalc ms.
    /// </summary>
    public ulong AutoHwmLastRecalcMs { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark last recalc reason.
    /// </summary>
    public uint AutoHwmLastRecalcReason { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark send blocked ratio ppm.
    /// </summary>
    public uint AutoHwmSendBlockedRatioPpm { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark deferred snd hwm.
    /// </summary>
    public int AutoHwmDeferredSndHwm { get; }
    /// <summary>
    /// Gets or sets the automatic high water mark deferred rcv hwm.
    /// </summary>
    public int AutoHwmDeferredRcvHwm { get; }
    /// <summary>
    /// Gets or sets the is ready.
    /// </summary>
    public bool IsReady => SourceKind == MonitorSourceKind.Socket
        && (StateFlags & 0x1u) != 0;

}
