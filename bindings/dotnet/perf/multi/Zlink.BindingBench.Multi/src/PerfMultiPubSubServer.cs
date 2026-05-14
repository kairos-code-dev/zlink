using System;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiPubSubServer
{
    private const string Topic = "bench";

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int pollTimeoutMs = ResolveMultiClientPollTimeoutMs(options);
        int probeCount = ResolveMultiOnewayLatencyProbeCount();
        int probeIntervalUs = ResolveMultiOnewayLatencyProbeIntervalUs();
        int probeSettleMs =
            ResolveMultiOnewayLatencyProbeSettleMs(durationSeconds);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeoutMs);
        server.Options.NoDrop = options.PubSubXpubNoDrop > 0;
        server.Options.Linger = TimeSpan.Zero;

        server.Bind(endpoint);
        ApplyAutoHwmMsgUnit(server, size);
        RecalculateAutoHwm(ctx);
        PrintAutoHwmSnapshot(server, "server", options.Transport, size);
        WriteStdoutLine($"READY,{endpoint}");

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

        if (!RunPublishPhase(server, payload, runId, size, ref seq,
                durationSeconds, controlState))
        {
            return 2;
        }

        if (!controlState.StopRequested)
        {
            Thread.Sleep(probeSettleMs);
            if (!RunLatencyProbePhase(server, payload, runId, size, ref seq,
                    probeCount, probeIntervalUs, controlState))
            {
                return 2;
            }
        }

        return 0;
    }

    private static bool PublishNoWait(PubSocket server, ReadOnlySpan<byte> payload)
    {
        try
        {
            using Message message = new(payload);
            return server.Publish(Topic).Message(message)
                .Flags(SendFlags.DontWait).Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static bool RunPublishPhase(PubSocket server, byte[] payload,
        uint runId, int size, ref ulong seq, int durationSeconds,
        RunnerControlState controlState)
    {
        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;

        // C perf contract: PUBSUB server publishes active messages until the
        // scripted duration ends and then exits. The client owns its duration
        // window and does not require a stop token for completion.
        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active,
                size, seq, EpochNs());
            if (PublishNoWait(server, payload.AsSpan()))
            {
                seq++;
                continue;
            }

            // EAGAIN with no_drop XPub: yield and retry without a
            // poller timer wait.
            System.Threading.Thread.Yield();
        }

        return true;
    }

    private static bool RunLatencyProbePhase(PubSocket server, byte[] payload,
        uint runId, int size, ref ulong seq, int probeCount, int intervalUs,
        RunnerControlState controlState)
    {
        long intervalTicks = Math.Max(1,
            intervalUs * Stopwatch.Frequency / 1_000_000L);
        long nextTicks = Stopwatch.GetTimestamp();
        for (int i = 0; i < probeCount && !controlState.StopRequested; i++)
        {
            WaitUntil(nextTicks);
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Cooldown,
                size, seq, EpochNs());
            if (PublishNoWait(server, payload.AsSpan()))
                seq++;
            nextTicks = Stopwatch.GetTimestamp() + intervalTicks;
        }

        return true;
    }

    private static void WaitUntil(long targetTicks)
    {
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= targetTicks)
                return;
            long remainingTicks = targetTicks - nowTicks;
            int sleepMs = (int)(remainingTicks * 1000 / Stopwatch.Frequency);
            if (sleepMs > 0)
                Thread.Sleep(sleepMs);
            else
                Thread.Yield();
        }
    }
}
