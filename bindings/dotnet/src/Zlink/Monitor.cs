// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class SocketMonitor : IDisposable, IAsyncDisposable
{
    private static readonly NativeMethods.ZlinkMonitorHandlerDelegate NativeIgnore =
        NativeMethods.zlink_monitor_ignore_handler;
    private static readonly NativeMethods.ZlinkMonitorHandlerDelegate NativeCallback =
        OnNativeEvent;
    public static readonly Action<MonitorEvent> IgnoreHandler = static _ => { };

    private IntPtr _handle;
    private Action<MonitorEvent>? _handler;
    private SynchronizationContext? _handlerContext;
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
        _handlerContext = useNativeIgnore ? null : SynchronizationContext.Current;
        if (!useNativeIgnore)
            EnsureSelfHandle();
        int rc = NativeMethods.zlink_socket_monitor_handler(_handle,
            useNativeIgnore ? NativeIgnore : NativeCallback,
            useNativeIgnore ? IntPtr.Zero : GCHandle.ToIntPtr(_selfHandle));
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public MonitorEvent Recv()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor_recv(_handle, out var native,
            0);
        if (rc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return MonitorEvent.FromNative(ref native);
    }

    public MonitorEvent? Recv(bool nonBlocking)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor_recv(_handle, out var native,
            nonBlocking ? 1 : 0);
        if (rc == 0)
            return MonitorEvent.FromNative(ref native);
        if (nonBlocking && ZlinkException.MapErrorCode(NativeMethods.zlink_errno())
            == ErrorCode.EAgain)
        {
            return null;
        }
        throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
    }

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
        _handlerContext = null;
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
            CallbackDelivery.Post(_handlerContext, () => handler(monitorEvent));
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
    public MonitorSnapshot(SourceKind sourceKind, uint stateFlags,
        uint detailFlags, ulong sndPendingMsgs, ulong rcvPendingMsgs,
        uint autoHwmEnabled, uint autoHwmRole,
        uint autoHwmManagedConnections, uint autoHwmActiveHwmConnections,
        uint autoHwmPlanningTransportConnections,
        uint autoHwmBaseFloorPerConnection, int autoHwmAppliedSndHwm,
        int autoHwmAppliedRcvHwm, int autoHwmRequestedSndBuf,
        int autoHwmRequestedRcvBuf, int autoHwmEffectiveSndBuf,
        int autoHwmEffectiveRcvBuf, ulong autoHwmTotalMemoryBudgetBytes,
        ulong autoHwmQueueBudgetBytes, ulong autoHwmTransportBudgetBytes,
        ulong autoHwmRuntimeReserveBytes, ulong autoHwmGroupBudgetBytes,
        ulong autoHwmGroupMessageSlots, ulong autoHwmEffectiveMessageBytes,
        ulong autoHwmControlBudgetBytes, ulong autoHwmRoutedBudgetBytes,
        ulong autoHwmFanoutBudgetBytes, ulong autoHwmRecvIngressBudgetBytes,
        uint autoHwmControlActiveConnections,
        uint autoHwmRoutedActiveConnections,
        uint autoHwmFanoutActiveConnections,
        uint autoHwmRecvIngressActiveConnections,
        ulong autoHwmEstimatedMaxMemoryBytes, ulong autoHwmLastRecalcMs,
        uint autoHwmLastRecalcReason, uint autoHwmSendBlockedRatioPpm)
    {
        SourceKind = sourceKind;
        StateFlags = stateFlags;
        DetailFlags = detailFlags;
        SndPendingMsgs = sndPendingMsgs;
        RcvPendingMsgs = rcvPendingMsgs;
        AutoHwmEnabled = autoHwmEnabled != 0;
        AutoHwmRole = autoHwmRole;
        AutoHwmManagedConnections = autoHwmManagedConnections;
        AutoHwmActiveHwmConnections = autoHwmActiveHwmConnections;
        AutoHwmPlanningTransportConnections = autoHwmPlanningTransportConnections;
        AutoHwmBaseFloorPerConnection = autoHwmBaseFloorPerConnection;
        AutoHwmAppliedSndHwm = autoHwmAppliedSndHwm;
        AutoHwmAppliedRcvHwm = autoHwmAppliedRcvHwm;
        AutoHwmRequestedSndBuf = autoHwmRequestedSndBuf;
        AutoHwmRequestedRcvBuf = autoHwmRequestedRcvBuf;
        AutoHwmEffectiveSndBuf = autoHwmEffectiveSndBuf;
        AutoHwmEffectiveRcvBuf = autoHwmEffectiveRcvBuf;
        AutoHwmTotalMemoryBudgetBytes = autoHwmTotalMemoryBudgetBytes;
        AutoHwmQueueBudgetBytes = autoHwmQueueBudgetBytes;
        AutoHwmTransportBudgetBytes = autoHwmTransportBudgetBytes;
        AutoHwmRuntimeReserveBytes = autoHwmRuntimeReserveBytes;
        AutoHwmGroupBudgetBytes = autoHwmGroupBudgetBytes;
        AutoHwmGroupMessageSlots = autoHwmGroupMessageSlots;
        AutoHwmEffectiveMessageBytes = autoHwmEffectiveMessageBytes;
        AutoHwmControlBudgetBytes = autoHwmControlBudgetBytes;
        AutoHwmRoutedBudgetBytes = autoHwmRoutedBudgetBytes;
        AutoHwmFanoutBudgetBytes = autoHwmFanoutBudgetBytes;
        AutoHwmRecvIngressBudgetBytes = autoHwmRecvIngressBudgetBytes;
        AutoHwmControlActiveConnections = autoHwmControlActiveConnections;
        AutoHwmRoutedActiveConnections = autoHwmRoutedActiveConnections;
        AutoHwmFanoutActiveConnections = autoHwmFanoutActiveConnections;
        AutoHwmRecvIngressActiveConnections = autoHwmRecvIngressActiveConnections;
        AutoHwmEstimatedMaxMemoryBytes = autoHwmEstimatedMaxMemoryBytes;
        AutoHwmLastRecalcMs = autoHwmLastRecalcMs;
        AutoHwmLastRecalcReason = autoHwmLastRecalcReason;
        AutoHwmSendBlockedRatioPpm = autoHwmSendBlockedRatioPpm;
    }

    public SourceKind SourceKind { get; }
    public uint StateFlags { get; }
    public uint DetailFlags { get; }
    public ulong SndPendingMsgs { get; }
    public ulong RcvPendingMsgs { get; }
    public bool AutoHwmEnabled { get; }
    public uint AutoHwmRole { get; }
    public uint AutoHwmManagedConnections { get; }
    public uint AutoHwmActiveHwmConnections { get; }
    public uint AutoHwmPlanningTransportConnections { get; }
    public uint AutoHwmBaseFloorPerConnection { get; }
    public int AutoHwmAppliedSndHwm { get; }
    public int AutoHwmAppliedRcvHwm { get; }
    public int AutoHwmRequestedSndBuf { get; }
    public int AutoHwmRequestedRcvBuf { get; }
    public int AutoHwmEffectiveSndBuf { get; }
    public int AutoHwmEffectiveRcvBuf { get; }
    public ulong AutoHwmTotalMemoryBudgetBytes { get; }
    public ulong AutoHwmQueueBudgetBytes { get; }
    public ulong AutoHwmTransportBudgetBytes { get; }
    public ulong AutoHwmRuntimeReserveBytes { get; }
    public ulong AutoHwmGroupBudgetBytes { get; }
    public ulong AutoHwmGroupMessageSlots { get; }
    public ulong AutoHwmEffectiveMessageBytes { get; }
    public ulong AutoHwmControlBudgetBytes { get; }
    public ulong AutoHwmRoutedBudgetBytes { get; }
    public ulong AutoHwmFanoutBudgetBytes { get; }
    public ulong AutoHwmRecvIngressBudgetBytes { get; }
    public uint AutoHwmControlActiveConnections { get; }
    public uint AutoHwmRoutedActiveConnections { get; }
    public uint AutoHwmFanoutActiveConnections { get; }
    public uint AutoHwmRecvIngressActiveConnections { get; }
    public ulong AutoHwmEstimatedMaxMemoryBytes { get; }
    public ulong AutoHwmLastRecalcMs { get; }
    public uint AutoHwmLastRecalcReason { get; }
    public uint AutoHwmSendBlockedRatioPpm { get; }
    public bool IsReady => SourceKind == SourceKind.Socket
        && (StateFlags & 0x1u) != 0;

    internal static MonitorSnapshot FromNative(ref ZlinkMonitorSnapshot native)
    {
        return new MonitorSnapshot((SourceKind)native.SourceKind,
            native.StateFlags, native.DetailFlags, native.SndPendingMsgs,
            native.RcvPendingMsgs, native.AutoHwmEnabled, native.AutoHwmRole,
            native.AutoHwmManagedConnections,
            native.AutoHwmActiveHwmConnections,
            native.AutoHwmPlanningTransportConnections,
            native.AutoHwmBaseFloorPerConnection,
            native.AutoHwmAppliedSndHwm, native.AutoHwmAppliedRcvHwm,
            native.AutoHwmRequestedSndBuf, native.AutoHwmRequestedRcvBuf,
            native.AutoHwmEffectiveSndBuf, native.AutoHwmEffectiveRcvBuf,
            native.AutoHwmTotalMemoryBudgetBytes,
            native.AutoHwmQueueBudgetBytes,
            native.AutoHwmTransportBudgetBytes,
            native.AutoHwmRuntimeReserveBytes,
            native.AutoHwmGroupBudgetBytes,
            native.AutoHwmGroupMessageSlots,
            native.AutoHwmEffectiveMessageBytes,
            native.AutoHwmControlBudgetBytes,
            native.AutoHwmRoutedBudgetBytes,
            native.AutoHwmFanoutBudgetBytes,
            native.AutoHwmRecvIngressBudgetBytes,
            native.AutoHwmControlActiveConnections,
            native.AutoHwmRoutedActiveConnections,
            native.AutoHwmFanoutActiveConnections,
            native.AutoHwmRecvIngressActiveConnections,
            native.AutoHwmEstimatedMaxMemoryBytes,
            native.AutoHwmLastRecalcMs,
            native.AutoHwmLastRecalcReason,
            native.AutoHwmSendBlockedRatioPpm);
    }
}
