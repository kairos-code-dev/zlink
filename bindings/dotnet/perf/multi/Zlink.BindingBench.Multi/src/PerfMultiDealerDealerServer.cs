using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerServer
{
    private const int SendPollTimeoutMs = 50;

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-dealer-dealer", options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new DealerSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        if (!controlState.WaitForStart(readyTimeoutMs))
        {
            if (!controlState.StopRequested)
                Console.Error.WriteLine("multi_server_error:no_start");
            return controlState.StopRequested ? 0 : 2;
        }

        var sockets = new[] { (SocketBase)server };
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        return RunPublishPhase(pollManager, server, sockets, payload, size,
            durationSeconds, controlState) ? 0 : 2;
    }

    private static bool RunPublishPhase(PollManager pollManager,
        DealerSocket server, IReadOnlyList<SocketBase> pollSockets,
        byte[] payload, int msgSize, int durationSeconds,
        RunnerControlState controlState)
    {
        const uint runId = 1;
        ulong seq = 1;
        bool sendPending = false;
        bool cooldownSent = false;
        long activeDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;

        while (!controlState.StopRequested)
        {
            bool inActivePhase = Stopwatch.GetTimestamp() < activeDeadlineTicks;
            if (!inActivePhase && cooldownSent)
                return true;

            bool trySend = !sendPending;
            if (!trySend)
            {
                int pollTimeoutMs = Math.Min(SendPollTimeoutMs,
                    RemainingMilliseconds(activeDeadlineTicks));
                if (PollSocketWriteReady(pollManager, pollSockets,
                        Math.Max(1, pollTimeoutMs)) > 0
                    && IsSocketWriteReady(pollManager, 0))
                {
                    trySend = true;
                }
            }

            if (trySend)
            {
                PerfPhase phase = inActivePhase
                    ? PerfPhase.Active
                    : PerfPhase.Cooldown;
                StampMetricHeader(payload.AsSpan(), runId, phase, msgSize, seq,
                    EpochNs());
                using Message message = Message.FromBytes(payload);
                if (server.Send(message, SendFlags.DontWait))
                {
                    seq++;
                    sendPending = false;
                    if (phase == PerfPhase.Cooldown)
                        cooldownSent = true;
                    continue;
                }

                sendPending = true;
            }

            int timeoutMs = Math.Min(SendPollTimeoutMs,
                RemainingMilliseconds(activeDeadlineTicks));
            if (timeoutMs <= 0)
                timeoutMs = 1;
            _ = PollSocketWriteReady(pollManager, pollSockets, timeoutMs);
        }

        return true;
    }

    private static int RemainingMilliseconds(long deadlineTicks)
    {
        long nowTicks = Stopwatch.GetTimestamp();
        if (deadlineTicks <= nowTicks)
            return 0;

        double remainingMs = (deadlineTicks - nowTicks) * 1000.0
            / Stopwatch.Frequency;
        if (remainingMs >= int.MaxValue)
            return int.MaxValue;
        return (int)Math.Ceiling(remainingMs);
    }
}
