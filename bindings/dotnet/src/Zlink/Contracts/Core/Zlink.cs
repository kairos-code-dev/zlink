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

    public static IContext CreateContext()
    {
        return new Context();
    }

    public static IAtomicCounter CreateAtomicCounter()
    {
        return new AtomicCounter();
    }

    public static IZlinkStopwatch CreateStopwatch()
    {
        return new ZlinkStopwatch();
    }

    public static IZlinkThread CreateThread(Action task)
    {
        return new ZlinkThread(task);
    }

    public static IPoller CreatePoller()
    {
        return new Poller();
    }

    public static IZlinkTimer CreateTimer()
    {
        return new Timer();
    }

    public static IZlinkTimer CreateTimer(ISpot spot)
    {
        return Timer.FromSpot(SocketInterop.RequireSpot(spot, nameof(spot)));
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
