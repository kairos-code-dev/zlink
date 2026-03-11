using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Zlink;

internal static partial class PerfRunner
{
    private const short PollIn = 0x0001;
    private const short PollOut = 0x0002;

    [StructLayout(LayoutKind.Sequential)]
    private struct ZlinkPollItemUnix
    {
        public IntPtr Socket;
        public int Fd;
        public short Events;
        public short Revents;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ZlinkPollItemWindows
    {
        public IntPtr Socket;
        public ulong Fd;
        public short Events;
        public short Revents;
    }

    private static class PerfNative
    {
        [DllImport("zlink", EntryPoint = "zlink_poll",
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int zlink_poll_unix(
            [In, Out] ZlinkPollItemUnix[] items, int nitems, long timeout);

        [DllImport("zlink", EntryPoint = "zlink_poll",
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int zlink_poll_windows(
            [In, Out] ZlinkPollItemWindows[] items, int nitems, long timeout);
    }

    private static ZlinkPollItemUnix[]? _pollItemsUnix;
    private static ZlinkPollItemWindows[]? _pollItemsWindows;
    private static ZlinkPollItemUnix[]? _socketPollItemsUnix;
    private static ZlinkPollItemWindows[]? _socketPollItemsWindows;

    internal static bool IsSupportedTransport(string transport)
    {
        return transport.Equals("tcp", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("ws", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("wss", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("inproc", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("ipc", StringComparison.OrdinalIgnoreCase);
    }

    internal static bool ParseEndpointArg(string endpoint,
        out string normalizedEndpoint)
    {
        normalizedEndpoint = endpoint?.Trim() ?? string.Empty;
        return !string.IsNullOrWhiteSpace(normalizedEndpoint);
    }

    internal static List<Zlink.Socket> WaitAllClientConnectReady(
        List<Zlink.Socket> clients, List<MonitorSocket> monitors,
        int readyTimeoutMs)
    {
        int count = Math.Min(clients.Count, monitors.Count);
        var activeClients = new List<Zlink.Socket>(count);
        if (count == 0)
            return activeClients;

        var ready = new bool[count];
        int remaining = count;
        var activeIndices = new int[count];
        int activeCount = 0;
        for (int i = 0; i < count; i++)
        {
            if (monitors[i] == null)
            {
                ready[i] = true;
                activeClients.Add(clients[i]);
                remaining--;
                continue;
            }
            activeIndices[activeCount++] = i;
        }

        if (remaining <= 0)
            return activeClients;

        long deadlineTicks = DeadlineTicksFromMilliseconds(readyTimeoutMs);

        while (remaining > 0)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                break;

            int readyEvents = PollMonitorHandles(monitors, activeIndices,
                activeCount, deadlineTicks, nowTicks);
            if (readyEvents < 0)
            {
                activeClients.Clear();
                return activeClients;
            }
            if (readyEvents == 0)
                continue;

            for (int i = 0; i < activeCount; i++)
            {
                int monitorIndex = activeIndices[i];
                if (monitorIndex < 0 || ready[monitorIndex])
                    continue;
                if (!IsPollInReady(i))
                    continue;

                if (!TryConsumeReadyEvent(monitors[monitorIndex]))
                    continue;

                ready[monitorIndex] = true;
                activeClients.Add(clients[monitorIndex]);
                remaining--;
                activeIndices[i] = -1;
            }
        }

        if (remaining > 0)
        {
            activeClients.Clear();
            return activeClients;
        }

        return activeClients;
    }

    private static int PollMonitorHandles(List<MonitorSocket> monitors,
        int[] activeIndices, int activeCount, long deadlineTicks, long nowTicks)
    {
        long remainingMs = (deadlineTicks - nowTicks) * 1000L
            / Stopwatch.Frequency;
        if (remainingMs < 0)
            remainingMs = 0;

        if (OperatingSystem.IsWindows())
        {
            if (_pollItemsWindows == null || _pollItemsWindows.Length < activeCount)
                _pollItemsWindows = new ZlinkPollItemWindows[activeCount];

            for (int i = 0; i < activeCount; i++)
            {
                ref ZlinkPollItemWindows item = ref _pollItemsWindows[i];
                int monitorIndex = activeIndices[i];
                if (monitorIndex < 0)
                {
                    item = default;
                    continue;
                }
                item.Socket = monitors[monitorIndex].Handle;
                item.Fd = 0;
                item.Events = PollIn;
                item.Revents = 0;
            }

            return PerfNative.zlink_poll_windows(_pollItemsWindows, activeCount,
                remainingMs);
        }

        if (_pollItemsUnix == null || _pollItemsUnix.Length < activeCount)
            _pollItemsUnix = new ZlinkPollItemUnix[activeCount];

        for (int i = 0; i < activeCount; i++)
        {
            ref ZlinkPollItemUnix item = ref _pollItemsUnix[i];
            int monitorIndex = activeIndices[i];
            if (monitorIndex < 0)
            {
                item = default;
                continue;
            }
            item.Socket = monitors[monitorIndex].Handle;
            item.Fd = 0;
            item.Events = PollIn;
            item.Revents = 0;
        }

        return PerfNative.zlink_poll_unix(_pollItemsUnix, activeCount,
            remainingMs);
    }

    private static bool IsPollInReady(int index)
    {
        if (OperatingSystem.IsWindows())
            return _pollItemsWindows != null
                && (_pollItemsWindows[index].Revents & PollIn) != 0;

        return _pollItemsUnix != null
            && (_pollItemsUnix[index].Revents & PollIn) != 0;
    }

    internal static int PollSocketReadReady(IReadOnlyList<Zlink.Socket> sockets,
        int timeoutMs)
    {
        return PollSocketEvents(sockets, PollIn, timeoutMs);
    }

    internal static int PollSocketWriteReady(IReadOnlyList<Zlink.Socket> sockets,
        int timeoutMs)
    {
        return PollSocketEvents(sockets, PollOut, timeoutMs);
    }

    private static int PollSocketEvents(IReadOnlyList<Zlink.Socket> sockets,
        short events, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0)
            return 0;

        long boundedTimeoutMs = Math.Max(0, timeoutMs);
        if (OperatingSystem.IsWindows())
        {
            if (_socketPollItemsWindows == null
                || _socketPollItemsWindows.Length < count)
            {
                _socketPollItemsWindows = new ZlinkPollItemWindows[count];
            }

            for (int i = 0; i < count; i++)
            {
                _socketPollItemsWindows[i].Socket = sockets[i].Handle;
                _socketPollItemsWindows[i].Fd = 0;
                _socketPollItemsWindows[i].Events = events;
                _socketPollItemsWindows[i].Revents = 0;
            }

            return PerfNative.zlink_poll_windows(_socketPollItemsWindows, count,
                boundedTimeoutMs);
        }

        if (_socketPollItemsUnix == null || _socketPollItemsUnix.Length < count)
            _socketPollItemsUnix = new ZlinkPollItemUnix[count];

        for (int i = 0; i < count; i++)
        {
            _socketPollItemsUnix[i].Socket = sockets[i].Handle;
            _socketPollItemsUnix[i].Fd = 0;
            _socketPollItemsUnix[i].Events = events;
            _socketPollItemsUnix[i].Revents = 0;
        }

        return PerfNative.zlink_poll_unix(_socketPollItemsUnix, count,
            boundedTimeoutMs);
    }

    internal static bool IsSocketReadReady(int index)
    {
        return IsSocketReady(index, PollIn);
    }

    internal static bool IsSocketWriteReady(int index)
    {
        return IsSocketReady(index, PollOut);
    }

    private static bool IsSocketReady(int index, short events)
    {
        if (OperatingSystem.IsWindows())
            return _socketPollItemsWindows != null
                && (_socketPollItemsWindows[index].Revents & events) != 0;

        return _socketPollItemsUnix != null
            && (_socketPollItemsUnix[index].Revents & events) != 0;
    }

    private static bool TryConsumeReadyEvent(MonitorSocket monitor)
    {
        return DrainReadyEvents(monitor, false) > 0;
    }

    private static int DrainReadyEvents(MonitorSocket monitor,
        bool acceptFallback)
    {
        int readyCount = 0;
        while (true)
        {
            try
            {
                MonitorEvent evt = monitor.Receive(ReceiveFlags.DontWait);
                if (IsMonitorReady(evt.Event, acceptFallback))
                    readyCount++;
            }
            catch (ZlinkException ex) when (ex.Errno == ErrnoEagain
                                            || ex.Errno == ErrnoEintr)
            {
                return readyCount;
            }
            catch (ObjectDisposedException)
            {
                return readyCount;
            }
        }
    }

    internal static void TrySendStopToken(IReadOnlyList<Zlink.Socket> activeClients)
    {
        if (activeClients == null || activeClients.Count == 0)
            return;

        for (int i = 0; i < activeClients.Count; i++)
        {
            try
            {
                _ = SendBlocking(activeClients[i], MultiStopToken.AsSpan(),
                    SendFlags.None);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }

    internal static void DisposeAllQuietly<T>(IEnumerable<T> resources)
        where T : class, IDisposable
    {
        foreach (T resource in resources)
            TryDisposeQuietly(resource);
    }

    internal static void TryDisposeQuietly(IDisposable? resource)
    {
        if (resource == null)
            return;

        try
        {
            resource.Dispose();
        }
        catch
        {
        }
    }
}
