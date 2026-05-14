// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class SocketMonitor : IDisposable, IAsyncDisposable
{
    private static readonly NativeMethods.ZlinkMonitorHandlerDelegate NativeIgnore =
        OnIgnoredNativeEvent;
    private static readonly NativeMethods.ZlinkMonitorHandlerDelegate NativeCallback =
        OnNativeEvent;
    public static readonly Action<MonitorEvent> IgnoreHandler = static _ => { };

    private IntPtr _handle;
    private Action<MonitorEvent>? _handler;
    private GCHandle _selfHandle;
    private bool _selfHandleAllocated;

    internal SocketMonitor(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid monitor handle.", nameof(handle));
        _handle = handle;
    }

    internal IntPtr Handle => _handle;

    public void OnEvent(Action<MonitorEvent> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        bool useNativeIgnore = ReferenceEquals(handler, IgnoreHandler);
        _handler = useNativeIgnore ? null : handler;
        if (!useNativeIgnore)
            EnsureSelfHandle();
        int rc = NativeMethods.zlink_socket_monitor_handler(_handle,
            useNativeIgnore ? NativeIgnore : NativeCallback,
            useNativeIgnore ? IntPtr.Zero : GCHandle.ToIntPtr(_selfHandle));
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public MonitorEvent? Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor_recv(_handle, out var native,
            (flags & RecvFlags.DontWait) != 0 ? 1 : 0);
        if (rc == 0)
            return MonitorEvent.FromNative(ref native);
        if ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(NativeMethods.zlink_errno()) == ErrorCode.EAgain)
        {
            return null;
        }
        throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
    }

    internal MonitorEvent? Recv(bool nonBlocking)
        => Recv(nonBlocking ? RecvFlags.DontWait : RecvFlags.None);

    internal bool RecvNoWait(out MonitorEvent? monitorEvent)
    {
        monitorEvent = Recv(true);
        return monitorEvent != null;
    }

    public MonitorSnapshot Snapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_monitor_snapshot(_handle, out var native);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return MonitorSnapshot.FromNative(ref native);
    }

    public void Close()
    {
        if (_handle == IntPtr.Zero)
            return;
        _handler = null;
        int rc = NativeMethods.zlink_monitor_close(ref _handle);
        if (rc != 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        ReleaseSelfHandle();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        try
        {
            Close();
        }
        finally
        {
            GC.SuppressFinalize(this);
        }
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~SocketMonitor()
    {
        try
        {
            Close();
        }
        catch
        {
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SocketMonitor));
    }

    private void EnsureSelfHandle()
    {
        if (_selfHandleAllocated)
            return;

        _selfHandle = GCHandle.Alloc(this, GCHandleType.Normal);
        _selfHandleAllocated = true;
    }

    private void ReleaseSelfHandle()
    {
        if (!_selfHandleAllocated)
            return;

        _selfHandle.Free();
        _selfHandle = default;
        _selfHandleAllocated = false;
    }

    private void OnNativeEventCore(ref ZlinkMonitorEvent native)
    {
        Action<MonitorEvent>? handler = _handler;
        if (handler == null)
            return;

        try
        {
            MonitorEvent monitorEvent = MonitorEvent.FromNative(ref native);
            handler(monitorEvent);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private static void OnNativeEvent(ref ZlinkMonitorEvent native, IntPtr userData)
    {
        if (userData == IntPtr.Zero)
            return;

        GCHandle handle = GCHandle.FromIntPtr(userData);
        if (handle.Target is SocketMonitor monitor)
            monitor.OnNativeEventCore(ref native);
    }

    private static void OnIgnoredNativeEvent(ref ZlinkMonitorEvent native,
        IntPtr userData)
    {
    }
}

public sealed record MonitorEvent(
    MonitorEventType Event,
    uint Value,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr)
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
}

public sealed class MonitorSnapshot
{
    internal MonitorSnapshot(MonitorSourceKind sourceKind, uint stateFlags,
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

    internal static MonitorSnapshot FromNative(ref ZlinkMonitorSnapshot native)
    {
        return new MonitorSnapshot((MonitorSourceKind)native.MonitorSourceKind,
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
