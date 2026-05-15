// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

public static class Zlink
{
    public static event Action<Exception>? UnhandledCallbackException
    {
        add => Runtime.UnhandledCallbackException += value;
        remove => Runtime.UnhandledCallbackException -= value;
    }

    internal static int Errno()
    {
        return ZlinkRuntime.Errno();
    }

    public static string Strerror(int errnum)
    {
        return ZlinkRuntime.Strerror(errnum);
    }

    public static (int Major, int Minor, int Patch) Version()
    {
        return ZlinkRuntime.Version();
    }

    public static bool Has(string capability)
    {
        return ZlinkRuntime.Has(capability);
    }

    public static void Proxy(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture = null)
    {
        ZlinkRuntime.Proxy(frontend, backend, capture);
    }

    public static void ProxySteerable(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture, IZlinkSocket control)
    {
        ZlinkRuntime.ProxySteerable(frontend, backend, capture, control);
    }

    public static void Sleep(TimeSpan duration)
    {
        ZlinkRuntime.Sleep(duration);
    }

    internal static void Sleep(int seconds)
    {
        ZlinkRuntime.Sleep(seconds);
    }

    public static void MultipartClose(IReadOnlyList<Message> parts)
    {
        ZlinkRuntime.MultipartClose(parts);
    }
}
