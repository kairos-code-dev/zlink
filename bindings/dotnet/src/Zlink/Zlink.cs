// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Zlink.Native;

namespace Zlink;

public static class Zlink
{
    internal static int Errno()
    {
        return NativeMethods.zlink_errno();
    }

    public static string Strerror(int errnum)
    {
        IntPtr ptr = NativeMethods.zlink_strerror(errnum);
        return ptr == IntPtr.Zero
            ? string.Empty
            : (System.Runtime.InteropServices.Marshal.PtrToStringAnsi(ptr)
                ?? string.Empty);
    }

    public static (int Major, int Minor, int Patch) Version()
    {
        NativeMethods.zlink_version(out int major, out int minor, out int patch);
        return (major, minor, patch);
    }

    public static bool Has(string capability)
    {
        if (capability == null)
            throw new ArgumentNullException(nameof(capability));

        int rc = NativeMethods.zlink_has(capability);
        if (rc < 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return rc != 0;
    }

    public static void Proxy(IZlinkSocket frontend, IZlinkSocket backend,
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
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public static void ProxySteerable(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture, IZlinkSocket control)
    {
        SocketBase frontendSocket = SocketInterop.RequireSocket(frontend,
            nameof(frontend));
        SocketBase backendSocket = SocketInterop.RequireSocket(backend,
            nameof(backend));
        SocketBase? captureSocket = capture == null
            ? null
            : SocketInterop.RequireSocket(capture, nameof(capture));
        SocketBase controlSocket = SocketInterop.RequireSocket(control,
            nameof(control));

        int rc = NativeMethods.zlink_proxy_steerable(frontendSocket.Handle,
            backendSocket.Handle, captureSocket?.Handle ?? IntPtr.Zero,
            controlSocket.Handle);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public static void Sleep(int seconds)
    {
        if (seconds < 0)
            throw new ArgumentOutOfRangeException(nameof(seconds));
        NativeMethods.zlink_sleep(seconds);
    }

    public static void MultipartClose(IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        foreach (Message? part in parts)
            part?.Dispose();
    }
}
