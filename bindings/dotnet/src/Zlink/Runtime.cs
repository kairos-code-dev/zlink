// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

internal static class Runtime
{
    public static event Action<Exception>? UnhandledCallbackException;

    internal static void ReportUnhandledCallbackException(Exception exception)
    {
        try
        {
            UnhandledCallbackException?.Invoke(exception);
        }
        catch
        {
        }
    }

    public static bool Has(string capability)
    {
        if (capability == null)
            throw new ArgumentNullException(nameof(capability));
        int rc = NativeMethods.zlink_has(capability);
        if (rc < 0)
            throw ZlinkException.FromLastError();
        return rc != 0;
    }

    public static void SleepSeconds(int seconds)
    {
        if (seconds < 0)
            throw new ArgumentOutOfRangeException(nameof(seconds));
        NativeMethods.zlink_sleep(seconds);
    }

    public static void Sleep(TimeSpan duration)
    {
        double totalSeconds = duration.TotalSeconds;
        if (double.IsNaN(totalSeconds) || double.IsInfinity(totalSeconds)
            || totalSeconds < 0 || totalSeconds > int.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(duration));
        NativeMethods.zlink_sleep((int)Math.Ceiling(totalSeconds));
    }

    public static int Proxy(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture = null)
    {
        SocketBase frontendSocket = SocketInterop.RequireSocket(frontend,
            nameof(frontend));
        SocketBase backendSocket = SocketInterop.RequireSocket(backend,
            nameof(backend));
        SocketBase? captureSocket = capture == null
            ? null
            : SocketInterop.RequireSocket(capture, nameof(capture));

        int rc = NativeMethods.zlink_proxy(frontendSocket.Handle,
            backendSocket.Handle, captureSocket?.Handle ?? IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public static int ProxySteerable(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket control, IZlinkSocket? capture = null)
    {
        SocketBase frontendSocket = SocketInterop.RequireSocket(frontend,
            nameof(frontend));
        SocketBase backendSocket = SocketInterop.RequireSocket(backend,
            nameof(backend));
        SocketBase controlSocket = SocketInterop.RequireSocket(control,
            nameof(control));
        SocketBase? captureSocket = capture == null
            ? null
            : SocketInterop.RequireSocket(capture, nameof(capture));

        int rc = NativeMethods.zlink_proxy_steerable(frontendSocket.Handle,
            backendSocket.Handle, captureSocket?.Handle ?? IntPtr.Zero,
            controlSocket.Handle);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }
}
