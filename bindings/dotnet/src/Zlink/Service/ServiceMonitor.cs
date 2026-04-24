// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class ServiceMonitor : IDisposable, IAsyncDisposable
{
    private static readonly NativeMethods.ZlinkServiceMonitorHandlerDelegate NativeCallback =
        OnNativeEvent;
    private IntPtr _handle;
    private Action<ServiceEvent>? _handler;
    private SynchronizationContext? _handlerContext;
    private GCHandle _selfHandle;
    private bool _selfHandleAllocated;

    internal ServiceMonitor(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid monitor handle.", nameof(handle));
        _handle = handle;
    }

    public void OnEvent(Action<ServiceEvent> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        SynchronizationContext? context = SynchronizationContext.Current;
        _handler = handler;
        _handlerContext = context;
        EnsureSelfHandle();
        int rc = NativeMethods.zlink_service_monitor_handler(_handle,
            NativeCallback, GCHandle.ToIntPtr(_selfHandle));
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public ServiceEvent Recv()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_service_monitor_recv(_handle,
            out var native, 0);
        if (rc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return ServiceEvent.FromNative(ref native);
    }

    public ServiceEvent? Recv(bool nonBlocking)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_service_monitor_recv(_handle, out var native,
            nonBlocking ? 1 : 0);
        if (rc == 0)
        {
            return ServiceEvent.FromNative(ref native);
        }
        if (nonBlocking && ZlinkException.MapErrorCode(NativeMethods.zlink_errno())
            == ErrorCode.EAgain)
        {
            return null;
        }
        throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
    }

    internal bool RecvNoWait(out ServiceEvent? monitorEvent)
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

    ~ServiceMonitor()
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
            throw new ObjectDisposedException(nameof(ServiceMonitor));
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

    private void OnNativeEventCore(ref ZlinkServiceEvent native)
    {
        Action<ServiceEvent>? handler = _handler;
        SynchronizationContext? context = _handlerContext;
        if (handler == null)
            return;

        try
        {
            ServiceEvent monitorEvent = ServiceEvent.FromNative(ref native);
            CallbackDelivery.Post(context, () => handler(monitorEvent));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private static void OnNativeEvent(ref ZlinkServiceEvent native, IntPtr userData)
    {
        if (userData == IntPtr.Zero)
            return;

        GCHandle handle = GCHandle.FromIntPtr(userData);
        if (handle.Target is ServiceMonitor monitor)
            monitor.OnNativeEventCore(ref native);
    }
}
