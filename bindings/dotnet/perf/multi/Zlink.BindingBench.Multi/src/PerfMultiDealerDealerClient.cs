using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerClient
{
    private const int ActiveDeadlineTag = -1;

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string endpoint = options.Endpoint;

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiClientContextOptions(ctx, options);
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new DealerSocket(ctx);
                ApplyMultiSocketOptions(client, options);
                ConfigureTlsClientIfNeeded(client, options.Transport);
                client.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
                client.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeoutMs);
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<SocketBase> activeClients = WaitClientConnectReadyAll(
                pollManager, clients, monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            DisposeAllQuietly(monitors);
            monitors.Clear();

            for (int i = 0; i < clients.Count; i++)
                ApplyAutoHwmMsgUnit(clients[i], size);
            RecalculateAutoHwm(ctx);
            if (clients.Count > 0)
                PrintAutoHwmSnapshot(clients[0], "endpoint",
                    options.Transport, size);

            WriteStdoutLine($"CLIENT_READY,{size}");

            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            if (!RunSendPhase(activeClients, size, durationSeconds,
                    controlState))
                return 2;

            return 0;
        }
        finally
        {
            DisposeAllQuietly(monitors);
            DisposeAllQuietly(clients);
        }
    }

    private static bool RunSendPhase(List<SocketBase> activeClients, int msgSize,
        int durationSeconds, RunnerControlState controlState)
    {
        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        long activeDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        using var activeTimer = new Systems.Zlink.Timer();
        using var sendPoller = new Poller();
        var sendEvents = new PollEvent[activeClients.Count + 1];
        var pending = new bool[activeClients.Count];
        int pendingCount = 0;

        for (int i = 0; i < activeClients.Count; i++)
            sendPoller.Add(activeClients[i], PollEventFlags.PollOut, i);
        sendPoller.Add(activeTimer, ActiveDeadlineTag);
        activeTimer.Start(TimeSpan.FromSeconds(Math.Max(1, durationSeconds)),
            1);

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < activeClients.Count; i++)
            {
                if (pending[i])
                    continue;

                DealerSocket socket = (DealerSocket)activeClients[i];
                while (!controlState.StopRequested
                       && Stopwatch.GetTimestamp() < activeDeadlineTicks)
                {
                    StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active,
                        msgSize, seq, EpochNs());
                    if (!TrySendNoWait(socket, payload.AsSpan()))
                    {
                        pending[i] = true;
                        pendingCount++;
                        break;
                    }

                    seq++;
                    progressed = true;
                }
            }

            if (!progressed && pendingCount > 0
                && Stopwatch.GetTimestamp() < activeDeadlineTicks)
            {
                if (!WaitForWritable(sendPoller, sendEvents, pending,
                        ref pendingCount))
                    break;
            }
        }

        return SendStopTokens(activeClients);
    }

    private static bool TrySendNoWait(DealerSocket socket, ReadOnlySpan<byte> payload)
    {
        try
        {
            using Message message = new(payload);
            return socket.Send().Message(message).Flags(SendFlags.DontWait)
                .Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static bool SendStopTokens(IReadOnlyList<SocketBase> activeClients)
    {
        for (int i = 0; i < activeClients.Count; i++)
        {
            var socket = (DealerSocket)activeClients[i];
            try
            {
                socket.Options.SendTimeout = null;
                _ = SendBlocking(socket, MultiStopToken.AsSpan(),
                    SendFlags.None);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine(
                    $"multi_client_error:stop_token_send_failed:{ex.Message}");
                return false;
            }
        }

        return true;
    }

    private static bool WaitForWritable(Poller poller, PollEvent[] events,
        bool[] pending, ref int pendingCount)
    {
        int written = poller.Wait(events,
            TimeSpan.FromMilliseconds(MultiClientPollTimeoutMs),
            out _);
        for (int i = 0; i < written; i++)
        {
            if (events[i].Tag is ActiveDeadlineTag)
            {
                _ = events[i].Timer?.Recv();
                return false;
            }

            if ((events[i].Revents & PollEventFlags.PollOut) == 0
                || events[i].Tag is not int index
                || index < 0
                || index >= pending.Length
                || !pending[index])
                continue;

            pending[index] = false;
            pendingCount--;
        }

        return true;
    }
}
