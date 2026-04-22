// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class SocketMonitor : IDisposable, IAsyncDisposable
{
    private static readonly NativeMethods.ZlinkMonitorHandlerDelegate NativeIgnore =
        NativeMethods.zlink_monitor_ignore_handler;
    public static readonly Action<MonitorEvent> IgnoreHandler = static _ => { };

    private IntPtr _handle;
    private NativeMethods.ZlinkMonitorHandlerDelegate? _handlerDelegate;
    private Action<MonitorEvent>? _handler;
    private SynchronizationContext? _handlerContext;

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
        _handlerDelegate = useNativeIgnore ? NativeIgnore : OnNativeEvent;
        int rc = NativeMethods.zlink_socket_monitor_handler(_handle,
            _handlerDelegate, IntPtr.Zero);
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
        int rc = NativeMethods.zlink_monitor_close(ref _handle);
        if (rc != 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        _handler = null;
        _handlerContext = null;
        _handlerDelegate = null;
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

    private void OnNativeEvent(ref ZlinkMonitorEvent native, IntPtr userData)
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
        uint detailFlags, ulong sndPendingMsgs, ulong rcvPendingMsgs)
    {
        SourceKind = sourceKind;
        StateFlags = stateFlags;
        DetailFlags = detailFlags;
        SndPendingMsgs = sndPendingMsgs;
        RcvPendingMsgs = rcvPendingMsgs;
    }

    public SourceKind SourceKind { get; }
    public uint StateFlags { get; }
    public uint DetailFlags { get; }
    public ulong SndPendingMsgs { get; }
    public ulong RcvPendingMsgs { get; }
    public bool IsReady => SourceKind == SourceKind.Socket
        && (StateFlags & 0x1u) != 0;

    internal static MonitorSnapshot FromNative(ref ZlinkMonitorSnapshot native)
    {
        return new MonitorSnapshot((SourceKind)native.SourceKind,
            native.StateFlags, native.DetailFlags, native.SndPendingMsgs,
            native.RcvPendingMsgs);
    }
}
