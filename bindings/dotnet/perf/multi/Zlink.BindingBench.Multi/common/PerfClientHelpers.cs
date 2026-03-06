using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
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
        long deadlineTicks = DeadlineTicksFromMilliseconds(readyTimeoutMs);

        while (remaining > 0 && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < count; i++)
            {
                if (ready[i])
                    continue;

                if (!TryConsumeReadyEvent(monitors[i]))
                    continue;

                ready[i] = true;
                activeClients.Add(clients[i]);
                remaining--;
                progressed = true;
            }

            if (!progressed)
                Thread.Yield();
        }

        return activeClients;
    }

    private static bool TryConsumeReadyEvent(MonitorSocket monitor)
    {
        const int maxEventsPerProbe = 8;
        for (int i = 0; i < maxEventsPerProbe; i++)
        {
            try
            {
                MonitorEvent evt = monitor.Receive(ReceiveFlags.DontWait);
                if (IsMonitorReady(evt.Event, true))
                    return true;
            }
            catch (ZlinkException ex) when (ex.Errno == ErrnoEagain
                                            || ex.Errno == ErrnoEintr)
            {
                return false;
            }
            catch (ObjectDisposedException)
            {
                return false;
            }
        }

        return false;
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
