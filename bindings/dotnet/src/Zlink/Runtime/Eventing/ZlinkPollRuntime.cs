// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class ZlinkPollRuntime
{
    [ThreadStatic] private static ZlinkPollItemUnix[]? _unixItems;

    [ThreadStatic] private static ZlinkPollItemWindows[]? _windowsItems;

    public static int Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs)
    {
        if (sockets == null)
            throw new ArgumentNullException(nameof(sockets));
        var events = new PollEventFlags[sockets.Count];
        var revents = sockets.Count <= 64
            ? stackalloc PollEventFlags[sockets.Count]
            : new PollEventFlags[sockets.Count];
        Array.Fill(events, PollEventFlags.PollIn);
        return PollSocketsCore(sockets.Count,
            i => SocketInterop.RequireSocket(sockets[i], nameof(sockets)).Handle,
            events, revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<IZlinkSocket> sockets,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        if (sockets == null)
            throw new ArgumentNullException(nameof(sockets));
        return PollSocketsCore(sockets.Count,
            i => SocketInterop.RequireSocket(sockets[i], nameof(sockets)).Handle,
            events, revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<ISocketMonitor> monitors,
        int timeoutMs)
    {
        if (monitors == null)
            throw new ArgumentNullException(nameof(monitors));
        var events = new PollEventFlags[monitors.Count];
        var revents = monitors.Count <= 64
            ? stackalloc PollEventFlags[monitors.Count]
            : new PollEventFlags[monitors.Count];
        Array.Fill(events, PollEventFlags.PollIn);
        return PollSocketsCore(monitors.Count,
            i => RequireMonitor(monitors[i], nameof(monitors)).Handle,
            events, revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<ISocketMonitor> monitors,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        if (monitors == null)
            throw new ArgumentNullException(nameof(monitors));
        return PollSocketsCore(monitors.Count,
            i => RequireMonitor(monitors[i], nameof(monitors)).Handle,
            events, revents, timeoutMs);
    }

    private static SocketMonitor RequireMonitor(ISocketMonitor monitor,
        string paramName)
    {
        if (monitor == null)
            throw new ArgumentNullException(paramName);
        if (monitor is not SocketMonitor concrete)
            throw new ArgumentException(
                "monitor must be a concrete zlink socket monitor instance",
                paramName);
        return concrete;
    }

    private static int PollSocketsCore(int count, Func<int, IntPtr> getHandle,
        IReadOnlyList<PollEventFlags> events, Span<PollEventFlags> revents,
        int timeoutMs)
    {
        if (getHandle == null)
            throw new ArgumentNullException(nameof(getHandle));
        if (events == null)
            throw new ArgumentNullException(nameof(events));
        if (events.Count < count)
            throw new ArgumentException("events length is too small.",
                nameof(events));
        if (revents.Length < count)
            throw new ArgumentException("revents length is too small.",
                nameof(revents));
        if (count <= 0)
            return 0;

        int rc;
        long boundedTimeoutMs = Math.Max(0, timeoutMs);
        if (OperatingSystem.IsWindows())
        {
            var items = EnsureWindowsCapacity(count);
            for (var i = 0; i < count; i++)
            {
                items[i].Socket = getHandle(i);
                items[i].Fd = 0;
                items[i].Events = (short)events[i];
                items[i].Revents = 0;
            }

            rc = NativeMethods.zlink_poll(items, count, boundedTimeoutMs,
                out _);
            if (rc < 0)
                throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
            for (var i = 0; i < count; i++)
                revents[i] = (PollEventFlags)items[i].Revents;
            return rc;
        }

        var unixItems = EnsureUnixCapacity(count);
        for (var i = 0; i < count; i++)
        {
            unixItems[i].Socket = getHandle(i);
            unixItems[i].Fd = 0;
            unixItems[i].Events = (short)events[i];
            unixItems[i].Revents = 0;
        }

        rc = NativeMethods.zlink_poll(unixItems, count, boundedTimeoutMs,
            out _);
        if (rc < 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        for (var i = 0; i < count; i++)
            revents[i] = (PollEventFlags)unixItems[i].Revents;
        return rc;
    }

    private static ZlinkPollItemUnix[] EnsureUnixCapacity(int count)
    {
        if (_unixItems == null || _unixItems.Length < count)
            _unixItems = new ZlinkPollItemUnix[count];
        return _unixItems;
    }

    private static ZlinkPollItemWindows[] EnsureWindowsCapacity(int count)
    {
        if (_windowsItems == null || _windowsItems.Length < count)
            _windowsItems = new ZlinkPollItemWindows[count];
        return _windowsItems;
    }
}