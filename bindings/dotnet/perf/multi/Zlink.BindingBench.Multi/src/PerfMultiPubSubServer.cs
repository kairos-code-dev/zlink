using System;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfMultiPubSubServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        server.SetOption(SocketOptions.XPubNoDrop,
            options.PubSubXpubNoDrop > 0 ? 1 : 0);

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

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

        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        var pollSockets = new[] { server };

        if (!RunPublishPhase(pollManager, server, pollSockets, payload, runId,
                size, ref seq, durationSeconds, controlState))
        {
            return 2;
        }

        return 0;
    }

    private static bool PublishNoWait(PubSocket server, ReadOnlySpan<byte> payload)
    {
        try
        {
            return server.PublishRawSingleNoWait(string.Empty, payload)
                == SendResult.Sent;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static bool RunPublishPhase(PollManager pollManager, PubSocket server,
        IReadOnlyList<SocketBase> pollSockets, byte[] payload, uint runId,
        int size, ref ulong seq, int durationSeconds,
        RunnerControlState controlState)
    {
        bool sendPending = false;
        bool cooldownSent = false;
        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;

        while (!controlState.StopRequested)
        {
            bool inActivePhase = Stopwatch.GetTimestamp() < deadlineTicks;
            if (!inActivePhase && cooldownSent)
                return true;

            bool trySend = !sendPending;
            if (!trySend)
            {
                int pollTimeoutMs = Math.Max(1,
                    Math.Min(50, RemainingMilliseconds(deadlineTicks)));
                if (PollSocketWriteReady(pollManager, pollSockets,
                        pollTimeoutMs) > 0
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
                StampMetricHeader(payload.AsSpan(), runId, phase, size, seq,
                    EpochNs());
                if (PublishNoWait(server, payload.AsSpan()))
                {
                    seq++;
                    sendPending = false;
                    if (phase == PerfPhase.Cooldown)
                        cooldownSent = true;
                    continue;
                }
                sendPending = true;
            }

            int timeoutMs = Math.Min(50, RemainingMilliseconds(deadlineTicks));
            if (timeoutMs <= 0)
                continue;

            if (sendPending)
            {
                _ = PollSocketWriteReady(pollManager, pollSockets,
                    Math.Max(1, timeoutMs));
            }
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
