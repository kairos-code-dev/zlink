using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
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
            || transport.Equals("wss", StringComparison.OrdinalIgnoreCase);
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
        return DrainReadyEvents(monitor) > 0;
    }

    internal static int DrainReadyEvents(MonitorSocket monitor)
    {
        int readyCount = 0;
        while (true)
        {
            try
            {
                MonitorEvent? evt = monitor.Recv(true);
                if (evt == null)
                    return readyCount;
                if (IsMonitorReady(evt.Event))
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
                    SendFlags.None);
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

    internal sealed class RunnerControlState : IDisposable
    {
        private readonly int _msgSize;
        private readonly bool _requirePhaseActive;
        private readonly ManualResetEventSlim _startSignal = new(false);
        private readonly Thread _readerThread;
        private int _startRequested;
        private int _phaseActiveRequested;
        private int _stopRequested;

        internal RunnerControlState(int msgSize, bool requirePhaseActive = false)
        {
            _msgSize = msgSize;
            _requirePhaseActive = requirePhaseActive;
            if (!requirePhaseActive)
                _phaseActiveRequested = 1;

            _readerThread = new Thread(ReadLoop)
            {
                IsBackground = true,
            };
            _readerThread.Start();
        }

        internal bool StopRequested => Volatile.Read(ref _stopRequested) != 0;

        internal bool WaitForStart(int timeoutMs)
        {
            if (_startSignal.IsSet)
                return true;

            int boundedTimeoutMs = Math.Max(1, timeoutMs);
            _startSignal.Wait(boundedTimeoutMs);
            return _startSignal.IsSet && !StopRequested;
        }

        public void Dispose()
        {
            _startSignal.Dispose();
        }

        private void ReadLoop()
        {
            string expectedStart = $"START,{_msgSize}";
            string expectedPhaseActive = $"PHASE_ACTIVE,{_msgSize}";

            while (true)
            {
                string? line = Console.ReadLine();
                if (line == null)
                {
                    Volatile.Write(ref _stopRequested, 1);
                    _startSignal.Set();
                    return;
                }

                string command = line.Trim();
                if (string.IsNullOrEmpty(command))
                    continue;

                if (string.Equals(command, "STOP",
                        StringComparison.OrdinalIgnoreCase)
                    || string.Equals(command, "QUIT",
                        StringComparison.OrdinalIgnoreCase))
                {
                    Volatile.Write(ref _stopRequested, 1);
                    _startSignal.Set();
                    return;
                }

                if (string.Equals(command, expectedStart,
                        StringComparison.Ordinal))
                {
                    Volatile.Write(ref _startRequested, 1);
                }
                else if (_requirePhaseActive
                         && string.Equals(command, expectedPhaseActive,
                             StringComparison.Ordinal))
                {
                    Volatile.Write(ref _phaseActiveRequested, 1);
                }

                if (Volatile.Read(ref _startRequested) != 0
                    && Volatile.Read(ref _phaseActiveRequested) != 0)
                {
                    _startSignal.Set();
                }
            }
        }
    }
}
