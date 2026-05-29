using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSocketMonitorOpenOptions
{
    public uint Events;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkMonitorStatus
{
    public int MonitorSourceKind;
    public uint StateFlags;
    public uint DetailFlags;
    public ulong SndPendingMsgs;
    public ulong RcvPendingMsgs;
    public uint AutoHwmEnabled;
    public uint AutoHwmProfile;
    public uint AutoHwmRole;
    public uint AutoHwmPolicyClass;
    public ulong AutoHwmUnitBudgetBytes;
    public uint AutoHwmSizeCap;
    public ulong AutoHwmSocketMessageSlots;
    public ulong AutoHwmEffectiveMessageBytes;
    public int AutoHwmAppliedSndHwm;
    public int AutoHwmAppliedRcvHwm;
    public int AutoHwmEffectiveSndbuf;
    public int AutoHwmEffectiveRcvbuf;
    public ulong AutoHwmLastRecalcMs;
    public uint AutoHwmLastRecalcReason;
    public uint AutoHwmSendBlockedRatioPpm;
    public int AutoHwmDeferredSndHwm;
    public int AutoHwmDeferredRcvHwm;
}
