using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;

internal static partial class PerfRunner
{
    internal const PollEvents SocketPollIn = PollEvents.PollIn;
    internal const PollEvents SocketPollOut = PollEvents.PollOut;

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

    internal static List<SocketBase> WaitAllClientConnectReady(
        PollManager pollManager, List<SocketBase> clients,
        List<MonitorSocket> monitors, int readyTimeoutMs)
    {
        int count = Math.Min(clients.Count, monitors.Count);
        var activeClients = new List<SocketBase>(count);
        if (count == 0)
            return activeClients;

        var ready = new bool[count];
        var pendingIndices = new List<int>(count);
        for (int i = 0; i < count; i++)
        {
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
                break;

            int pollCount = pendingIndices.Count;
            var pollMonitors = new List<MonitorSocket>(pollCount);
            var pollIndices = new int[pollCount];
            for (int i = 0; i < pollCount; i++)
            {
                pollMonitors.Add(monitors[pendingIndices[i]]);
                pollIndices[i] = i;
            }

            int readyEvents = pollManager.PollMonitors(pollMonitors,
                pollIndices, pollCount, deadlineTicks, nowTicks);
            if (readyEvents <= 0)
                continue;

            for (int i = pollCount - 1; i >= 0; i--)
            {
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

    internal static int PollMonitorHandles(PollManager pollManager,
        List<MonitorSocket> monitors, int[] activeIndices, int activeCount,
        long deadlineTicks, long nowTicks)
    {
        return pollManager.PollMonitors(monitors, activeIndices, activeCount,
            deadlineTicks, nowTicks);
    }

    internal static int PollSocketReadReady(PollManager pollManager,
        IReadOnlyList<SocketBase> sockets, int timeoutMs)
    {
        return pollManager.PollSockets(sockets, PollEvents.PollIn, timeoutMs);
    }

    internal static int PollSocketWriteReady(PollManager pollManager,
        IReadOnlyList<SocketBase> sockets, int timeoutMs)
    {
        return pollManager.PollSockets(sockets, PollEvents.PollOut, timeoutMs);
    }

    internal static int PollSocketEvents(PollManager pollManager,
        IReadOnlyList<SocketBase> sockets, IReadOnlyList<PollEvents> eventMasks,
        int timeoutMs)
    {
        return pollManager.PollSockets(sockets, eventMasks, timeoutMs);
    }

    internal static bool IsSocketReadReady(PollManager pollManager, int index)
    {
        return pollManager.IsSocketReadReady(index);
    }

    internal static bool IsSocketWriteReady(PollManager pollManager, int index)
    {
        return pollManager.IsSocketWriteReady(index);
    }

    private static bool TryConsumeReadyEvent(MonitorSocket monitor)
    {
        return DrainReadyEvents(monitor, false) > 0;
    }

    internal static int DrainReadyEvents(MonitorSocket monitor,
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
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                return readyCount;
            }
            catch (ObjectDisposedException)
            {
                return readyCount;
            }
        }
    }

    internal static void TrySendStopToken(IReadOnlyList<SocketBase> activeClients)
    {
        if (activeClients == null || activeClients.Count == 0)
            return;

        for (int i = 0; i < activeClients.Count; i++)
        {
            try
            {
                _ = SendBlocking(activeClients[i], MultiStopToken.AsSpan(),
                    PerfSendFlags.None);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
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
}
