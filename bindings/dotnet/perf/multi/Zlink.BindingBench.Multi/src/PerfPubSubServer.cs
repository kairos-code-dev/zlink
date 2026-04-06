using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSubServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int warmupSeconds = ResolveMultiWarmupSeconds(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-pubsub",
            options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new PubSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        server.SetOption(SocketOptions.XPubNoDrop,
            options.PubSubXpubNoDrop > 0 ? 1 : 0);

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        var pollSockets = new[] { server };

        if (warmupSeconds > 0)
        {
            long warmupDeadline = Stopwatch.GetTimestamp()
                + (long)warmupSeconds * Stopwatch.Frequency;
            RunPublishPhase(pollManager, server, pollSockets, payload, runId,
                size, PerfPhase.Warmup, ref seq, warmupDeadline);
        }

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        long activeSent = RunPublishPhase(pollManager, server, pollSockets,
            payload, runId, size, PerfPhase.Active, ref seq, benchDeadlineTicks);

        if (activeSent > 0)
        {
            double configuredSeconds = Math.Max(1.0, durationSeconds);
            double throughput = activeSent / configuredSeconds;
            double latencyUs = (configuredSeconds * 1_000_000.0)
                / Math.Max(1.0, activeSent);
            PrintResult(options.Pattern, options.Transport, size, throughput, latencyUs,
                latencyUs, latencyUs);
        }

        return 0;
    }

    private static bool TryPublish(SocketBase server, ReadOnlySpan<byte> payload)
    {
        return server.TrySend(payload, SendFlags.DontWait, out int written)
            && written > 0;
    }

    private static long RunPublishPhase(PollManager pollManager, SocketBase server,
        IReadOnlyList<SocketBase> pollSockets, byte[] payload, uint runId,
        int size, PerfPhase phase, ref ulong seq, long deadlineTicks)
    {
        bool sendPending = false;
        long sent = 0;

        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool trySend = !sendPending;
            if (!trySend)
            {
                int pollTimeoutMs = RemainingMilliseconds(deadlineTicks);
                if (PollSocketWriteReady(pollManager, pollSockets,
                        pollTimeoutMs) > 0
                    && IsSocketWriteReady(pollManager, 0))
                {
                    trySend = true;
                }
            }

            if (trySend)
            {
                StampMetricHeader(payload.AsSpan(), runId, phase, size, seq,
                    EpochUs());
                if (TryPublish(server, payload.AsSpan()))
                {
                    seq++;
                    sent++;
                    sendPending = false;
                    continue;
                }
                sendPending = true;
            }

            int timeoutMs = Math.Min(50, RemainingMilliseconds(deadlineTicks));
            if (timeoutMs <= 0)
                continue;

            if (sendPending)
            {
                _ = PollSocketWriteReady(pollManager, pollSockets, timeoutMs);
            }
            else
            {
                Thread.Sleep(timeoutMs);
            }
        }

        return sent;
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
