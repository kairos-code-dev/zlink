// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public static class ZlinkPoll
{
    [ThreadStatic]
    private static ZlinkPollItemUnix[]? _unixItems;

    [ThreadStatic]
    private static ZlinkPollItemWindows[]? _windowsItems;

    public static int Poll(IReadOnlyList<IZlinkSocket> sockets, int timeoutMs)
    {
        if (sockets == null)
            throw new ArgumentNullException(nameof(sockets));
        PollEvents[] events = new PollEvents[sockets.Count];
        Span<PollEvents> revents = sockets.Count <= 64
            ? stackalloc PollEvents[sockets.Count]
            : new PollEvents[sockets.Count];
        Array.Fill(events, PollEvents.PollIn);
        return PollSocketsCore(sockets.Count,
            i => SocketInterop.RequireSocket(sockets[i], nameof(sockets)).Handle,
            events, revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<IZlinkSocket> sockets,
        IReadOnlyList<PollEvents> events, Span<PollEvents> revents,
        int timeoutMs)
    {
        if (sockets == null)
            throw new ArgumentNullException(nameof(sockets));
        return PollSocketsCore(sockets.Count,
            i => SocketInterop.RequireSocket(sockets[i], nameof(sockets)).Handle,
            events, revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<SocketMonitor> monitors, int timeoutMs)
    {
        if (monitors == null)
            throw new ArgumentNullException(nameof(monitors));
        PollEvents[] events = new PollEvents[monitors.Count];
        Span<PollEvents> revents = monitors.Count <= 64
            ? stackalloc PollEvents[monitors.Count]
            : new PollEvents[monitors.Count];
        Array.Fill(events, PollEvents.PollIn);
        return PollSocketsCore(monitors.Count, i => monitors[i].Handle, events,
            revents, timeoutMs);
    }

    public static int Poll(IReadOnlyList<SocketMonitor> monitors,
        IReadOnlyList<PollEvents> events, Span<PollEvents> revents,
        int timeoutMs)
    {
        if (monitors == null)
            throw new ArgumentNullException(nameof(monitors));
        return PollSocketsCore(monitors.Count, i => monitors[i].Handle, events,
            revents, timeoutMs);
    }

    private static int PollSocketsCore(int count, Func<int, IntPtr> getHandle,
        IReadOnlyList<PollEvents> events, Span<PollEvents> revents,
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
            ZlinkPollItemWindows[] items = EnsureWindowsCapacity(count);
            for (int i = 0; i < count; i++)
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
            for (int i = 0; i < count; i++)
                revents[i] = (PollEvents)items[i].Revents;
            return rc;
        }

        ZlinkPollItemUnix[] unixItems = EnsureUnixCapacity(count);
        for (int i = 0; i < count; i++)
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
        for (int i = 0; i < count; i++)
            revents[i] = (PollEvents)unixItems[i].Revents;
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
