using System;
using System.Diagnostics;
using Systems.Zlink;
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
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
        server.Options.NoDrop = options.PubSubXpubNoDrop > 0;

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Bind(endpoint);
        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);
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
                size, ref seq, durationSeconds, pollTimeoutMs, controlState))
        {
            return 2;
        }

        return 0;
    }

    private static bool PublishNoWait(PubSocket server, ReadOnlySpan<byte> payload)
    {
        try
        {
            using Message message = Message.FromBytes(payload);
            return server.Publish(string.Empty, message, SendFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static bool RunPublishPhase(PollManager pollManager, PubSocket server,
        IReadOnlyList<SocketBase> pollSockets, byte[] payload, uint runId,
        int size, ref ulong seq, int durationSeconds, int pollTimeoutMs,
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
                int cappedMs = CapPollTimeoutMs(pollTimeoutMs, deadlineTicks);
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

            int timeoutMs = CapPollTimeoutMs(pollTimeoutMs, deadlineTicks);
            if (sendPending)
            {
                _ = PollSocketWriteReady(pollManager, pollSockets, timeoutMs);
            }
        }

        return true;
    }

}
