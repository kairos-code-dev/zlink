// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink.Service;

public sealed class ServiceMonitor : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;
    private NativeMethods.ZlinkServiceMonitorHandlerDelegate? _handlerDelegate;
    private Action<ServiceMonitorEvent>? _handler;
    private SynchronizationContext? _handlerContext;

    internal ServiceMonitor(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid monitor handle.", nameof(handle));
        _handle = handle;
    }

    public void OnEvent(Action<ServiceMonitorEvent> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        SynchronizationContext? context = SynchronizationContext.Current;
        _handler = handler;
        _handlerContext = context;
        _handlerDelegate = OnNativeEvent;
        int rc = NativeMethods.zlink_service_monitor_handler(_handle,
            _handlerDelegate, IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
    }

    public ServiceMonitorEvent Recv()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_service_monitor_recv(_handle,
            out var native, 0);
        ZlinkException.ThrowIfError(rc);
        return ServiceMonitorEvent.FromNative(ref native);
    }

    public ServiceMonitorEvent? Recv(bool nonBlocking)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_service_monitor_recv(_handle, out var native,
            nonBlocking ? 1 : 0);
        if (rc == 0)
        {
            return ServiceMonitorEvent.FromNative(ref native);
        }
        if (nonBlocking && ZlinkException.MapErrorCode(NativeMethods.zlink_errno())
            == ErrorCode.EAgain)
        {
            return null;
        }
        throw ZlinkException.FromLastError();
    }

    internal bool TryRecv(out ServiceMonitorEvent? monitorEvent)
    {
        monitorEvent = Recv(true);
        return monitorEvent != null;
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

    private void OnNativeEvent(ref ZlinkServiceEvent native, IntPtr userData)
    {
        Action<ServiceMonitorEvent>? handler = _handler;
        SynchronizationContext? context = _handlerContext;
        if (handler == null)
            return;

        try
        {
            ServiceMonitorEvent monitorEvent =
                ServiceMonitorEvent.FromNative(ref native);
            CallbackDelivery.Post(context, () => handler(monitorEvent));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }
}
