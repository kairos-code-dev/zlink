using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
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

        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);

        var sockets = new[] { (SocketBase)server };
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        return RunPublishPhase(pollManager, server, sockets, payload, size,
            durationSeconds, pollTimeoutMs, controlState) ? 0 : 2;
    }

    private static bool RunPublishPhase(PollManager pollManager,
        DealerSocket server, IReadOnlyList<SocketBase> pollSockets,
        byte[] payload, int msgSize, int durationSeconds, int pollTimeoutMs,
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
                int cappedMs = CapPollTimeoutMs(pollTimeoutMs, activeDeadlineTicks);
                if (PollSocketWriteReady(pollManager, pollSockets, cappedMs) > 0
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

            int timeoutMs = CapPollTimeoutMs(pollTimeoutMs, activeDeadlineTicks);
            _ = PollSocketWriteReady(pollManager, pollSockets, timeoutMs);
        }

        return true;
    }

}
