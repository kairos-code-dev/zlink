// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

public static class Runtime
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

    public static int Proxy(Socket frontend, Socket backend,
        Socket? capture = null)
    {
        if (frontend == null)
            throw new ArgumentNullException(nameof(frontend));
        if (backend == null)
            throw new ArgumentNullException(nameof(backend));

        int rc = NativeMethods.zlink_proxy(frontend.Handle, backend.Handle,
            capture?.Handle ?? IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public static int ProxySteerable(Socket frontend, Socket backend,
        Socket control, Socket? capture = null)
    {
        if (frontend == null)
            throw new ArgumentNullException(nameof(frontend));
        if (backend == null)
            throw new ArgumentNullException(nameof(backend));
        if (control == null)
            throw new ArgumentNullException(nameof(control));

        int rc = NativeMethods.zlink_proxy_steerable(frontend.Handle,
            backend.Handle, capture?.Handle ?? IntPtr.Zero, control.Handle);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }
}
