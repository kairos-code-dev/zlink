// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers.Binary;
using Zlink.Native;

namespace Zlink;

public sealed class SocketMonitor : IDisposable
{
    private IntPtr _handle;
    private NativeMethods.ZlinkMonitorHandlerDelegate? _handlerDelegate;
    private Action<SocketMonitorEvent>? _handler;

    internal SocketMonitor(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid monitor handle.", nameof(handle));
        _handle = handle;
    }

    internal IntPtr Handle => _handle;

    public void OnEvent(Action<SocketMonitorEvent> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        _handler = handler;
        _handlerDelegate = OnNativeEvent;
        int rc = NativeMethods.zlink_socket_monitor_handler(_handle,
            _handlerDelegate, IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
    }

    public SocketMonitorEvent Recv()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor_recv(_handle, out var native,
            0);
        ZlinkException.ThrowIfError(rc);
        return SocketMonitorEvent.FromNative(ref native);
    }

    public bool TryRecv(out SocketMonitorEvent? monitorEvent)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor_recv(_handle, out var native,
            1);
        if (rc == 0)
        {
            monitorEvent = SocketMonitorEvent.FromNative(ref native);
            return true;
        }
        if (ZlinkException.MapErrorCode(NativeMethods.zlink_errno())
            == ErrorCode.EAgain)
        {
            monitorEvent = null;
            return false;
        }
        monitorEvent = null;
        throw ZlinkException.FromLastError();
    }

    public MonitorSnapshot Snapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_monitor_snapshot(_handle, out var native);
        ZlinkException.ThrowIfError(rc);
        return MonitorSnapshot.FromNative(ref native);
    }

    public void Close()
    {
        if (_handle == IntPtr.Zero)
            return;
        int rc = NativeMethods.zlink_monitor_close(ref _handle);
        ZlinkException.ThrowIfError(rc);
        _handler = null;
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
        Action<SocketMonitorEvent>? handler = _handler;
        if (handler == null)
            return;

        try
        {
            handler(SocketMonitorEvent.FromNative(ref native));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }
}

public readonly struct SocketMonitorEvent
{
    public SocketMonitorEvent(SocketEvent @event, ulong value,
        string routingId,
        uint? streamRoutingId,
        string localAddress, string remoteAddress, ulong rawEvent)
    {
        Event = @event;
        Value = value;
        RoutingId = routingId;
        StreamRoutingId = streamRoutingId;
        LocalAddress = localAddress;
        RemoteAddress = remoteAddress;
        RawEvent = rawEvent;
    }

    public SocketEvent Event { get; }
    public ulong Value { get; }
    public string RoutingId { get; }
    public uint? StreamRoutingId { get; }
    public string LocalAddress { get; }
    public string RemoteAddress { get; }
    public ulong RawEvent { get; }

    internal static SocketMonitorEvent FromNative(ref ZlinkMonitorEvent evt)
    {
        byte[] routing = NativeHelpers.ReadRoutingId(ref evt.RoutingId);
        string routingId = RoutingIdCodec.ToPublicString(routing);
        uint? streamRoutingId = routing.Length == sizeof(uint)
            ? BinaryPrimitives.ReadUInt32BigEndian(routing)
            : null;
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
        SocketEvent @event = (SocketEvent)(evt.Event & 0xFFFFFFFFuL);
        return new SocketMonitorEvent(@event, evt.Value, routingId,
            streamRoutingId, local, remote, evt.Event);
    }
}

public readonly struct MonitorSnapshot
{
    public MonitorSnapshot(MonitorSourceKind sourceKind, MonitorState stateFlags,
        MonitorSnapshotDetail detailFlags,
        ulong sendPendingMessages, ulong receivePendingMessages)
    {
        SourceKind = sourceKind;
        StateFlags = stateFlags;
        DetailFlags = detailFlags;
        SendPendingMessages = sendPendingMessages;
        ReceivePendingMessages = receivePendingMessages;
    }

    public MonitorSourceKind SourceKind { get; }
    public MonitorState StateFlags { get; }
    public MonitorSnapshotDetail DetailFlags { get; }
    public ulong SendPendingMessages { get; }
    public ulong ReceivePendingMessages { get; }

    internal static MonitorSnapshot FromNative(ref ZlinkMonitorSnapshot native)
    {
        return new MonitorSnapshot((MonitorSourceKind)native.SourceKind,
            (MonitorState)native.StateFlags,
            (MonitorSnapshotDetail)native.DetailFlags,
            native.SndPendingMsgs, native.RcvPendingMsgs);
    }
}
