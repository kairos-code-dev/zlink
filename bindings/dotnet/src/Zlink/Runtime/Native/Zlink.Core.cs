// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public static partial class Zlink
{
    private static string StrerrorCore(int errnum)
    {
        return ZlinkRuntime.Strerror(errnum);
    }

    private static (int Major, int Minor, int Patch) VersionCore()
    {
        return ZlinkRuntime.Version();
    }

    private static bool HasCore(string capability)
    {
        return ZlinkRuntime.Has(capability);
    }

    private static void ProxyCore(IZlinkSocket frontend, IZlinkSocket backend,
        IZlinkSocket? capture)
    {
        ZlinkRuntime.Proxy(frontend, backend, capture);
    }

    private static void ProxySteerableCore(IZlinkSocket frontend,
        IZlinkSocket backend, IZlinkSocket? capture, IZlinkSocket control)
    {
        ZlinkRuntime.ProxySteerable(frontend, backend, capture, control);
    }

    private static void SleepCore(TimeSpan duration)
    {
        ZlinkRuntime.Sleep(duration);
    }

    private static void MultipartCloseCore(IReadOnlyList<Message> parts)
    {
        ZlinkRuntime.MultipartClose(parts);
    }
}