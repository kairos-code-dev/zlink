using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;

internal static partial class PerfRunner
{
    internal const PollEvents SocketPollIn = PollEvents.PollIn;
    internal const PollEvents SocketPollOut = PollEvents.PollOut;

    private static PollEvents[]? _monitorPollEvents;
    private static PollEvents[]? _monitorRevents;
    private static PollEvents[]? _socketPollEvents;
    private static PollEvents[]? _socketRevents;

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
        var pendingIndices = new List<int>(count);
        for (int i = 0; i < count; i++)
        {
            if (monitors[i] == null)
            {
                ready[i] = true;
                continue;
            }

            if (TryConsumeReadyEvent(monitors[i]))
            {
                ready[i] = true;
                continue;
            }

            pendingIndices.Add(i);
        }

        if (pendingIndices.Count == 0)
        {
            for (int i = 0; i < count; i++)
                if (ready[i])
                    activeClients.Add(clients[i]);
            return activeClients;
        }

        long deadlineTicks = DeadlineTicksFromMilliseconds(readyTimeoutMs);
        while (pendingIndices.Count > 0)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
            {
                activeClients.Clear();
                return activeClients;
            }

            long remainingMs = (deadlineTicks - nowTicks) * 1000L
                / Stopwatch.Frequency;
            int pollCount = pendingIndices.Count;
            EnsureMonitorPollCapacity(pollCount);

            var pollMonitors = new List<MonitorSocket>(pollCount);
            for (int i = 0; i < pollCount; i++)
            {
                pollMonitors.Add(monitors[pendingIndices[i]]);
                _monitorPollEvents![i] = PollEvents.PollIn;
                _monitorRevents![i] = PollEvents.None;
            }

            int readyEvents = ZlinkPoll.Poll(pollMonitors, _monitorPollEvents!,
                _monitorRevents!.AsSpan(0, pollCount), (int)Math.Max(0, remainingMs));
            if (readyEvents <= 0)
                continue;

            for (int i = pollCount - 1; i >= 0; i--)
            {
                if ((_monitorRevents![i] & PollEvents.PollIn) == 0)
                    continue;

                int index = pendingIndices[i];
                if (!TryConsumeReadyEvent(monitors[index]))
                    continue;

                ready[index] = true;
                pendingIndices.RemoveAt(i);
            }
        }

        for (int i = 0; i < count; i++)
        {
            if (ready[i])
                activeClients.Add(clients[i]);
        }

        return activeClients;
    }

    private static int PollMonitorHandles(List<MonitorSocket> monitors,
        int[] activeIndices, int activeCount, long deadlineTicks, long nowTicks)
    {
        if (activeCount <= 0)
            return 0;

        long remainingMs = (deadlineTicks - nowTicks) * 1000L
            / Stopwatch.Frequency;
        EnsureMonitorPollCapacity(activeCount);

        var pollMonitors = new List<MonitorSocket>(activeCount);
        for (int i = 0; i < activeCount; i++)
        {
            pollMonitors.Add(monitors[activeIndices[i]]);
            _monitorPollEvents![i] = PollEvents.PollIn;
            _monitorRevents![i] = PollEvents.None;
        }

        return ZlinkPoll.Poll(pollMonitors, _monitorPollEvents!,
            _monitorRevents!.AsSpan(0, activeCount), (int)Math.Max(0, remainingMs));
    }

    internal static int PollSocketReadReady(IReadOnlyList<Zlink.Socket> sockets,
        int timeoutMs)
    {
        return PollSocketEvents(sockets, PollEvents.PollIn, timeoutMs);
    }

    internal static int PollSocketWriteReady(IReadOnlyList<Zlink.Socket> sockets,
        int timeoutMs)
    {
        return PollSocketEvents(sockets, PollEvents.PollOut, timeoutMs);
    }

    internal static int PollSocketEvents(IReadOnlyList<Zlink.Socket> sockets,
        IReadOnlyList<PollEvents> eventMasks, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0 || eventMasks.Count < count)
            return 0;

        EnsureSocketPollCapacity(count);
        return ZlinkPoll.Poll(sockets, eventMasks,
            _socketRevents!.AsSpan(0, count), timeoutMs);
    }

    private static int PollSocketEvents(IReadOnlyList<Zlink.Socket> sockets,
        PollEvents events, int timeoutMs)
    {
        int count = sockets.Count;
        if (count <= 0)
            return 0;

        EnsureSocketPollCapacity(count);
        for (int i = 0; i < count; i++)
            _socketPollEvents![i] = events;
        return ZlinkPoll.Poll(sockets, _socketPollEvents!,
            _socketRevents!.AsSpan(0, count), timeoutMs);
    }

    internal static bool IsSocketReadReady(int index)
    {
        return IsSocketReady(index, PollEvents.PollIn);
    }

    internal static bool IsSocketWriteReady(int index)
    {
        return IsSocketReady(index, PollEvents.PollOut);
    }

    private static bool IsSocketReady(int index, PollEvents events)
    {
        return _socketRevents != null
            && index >= 0
            && index < _socketRevents.Length
            && (_socketRevents[index] & events) != 0;
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

    private static void EnsureMonitorPollCapacity(int count)
    {
        if (_monitorPollEvents == null || _monitorPollEvents.Length < count)
            _monitorPollEvents = new PollEvents[count];
        if (_monitorRevents == null || _monitorRevents.Length < count)
            _monitorRevents = new PollEvents[count];
    }

    private static void EnsureSocketPollCapacity(int count)
    {
        if (_socketPollEvents == null || _socketPollEvents.Length < count)
            _socketPollEvents = new PollEvents[count];
        if (_socketRevents == null || _socketRevents.Length < count)
            _socketRevents = new PollEvents[count];
    }
}
