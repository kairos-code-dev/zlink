using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSubServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "PUBSUB";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int sndTimeoutMs = ResolveMultiSndTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int clientCount = ResolveMultiClients(pattern);
        string endpoint = MultiEndpointFor(transport, "multi-pubsub");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Pub);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        server.SetOption(SocketOptions.XPubNoDrop,
            ParsePositiveEnv("PERF_PUBSUB_XPUB_NODROP", 1) > 0 ? 1 : 0);

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
            RunPublishPhase(server, pollSockets, payload, runId, size,
                PerfPhase.Warmup, ref seq, warmupDeadline);
        }

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        long activeSent = RunPublishPhase(server, pollSockets, payload, runId,
            size, PerfPhase.Active, ref seq, benchDeadlineTicks);

        if (activeSent > 0)
        {
            double configuredSeconds = Math.Max(1.0, durationSeconds);
            double throughput = activeSent / configuredSeconds;
            double latencyUs = (configuredSeconds * 1_000_000.0)
                / Math.Max(1.0, activeSent);
            PrintResult(pattern, transport, size, throughput, latencyUs,
                latencyUs, latencyUs);
        }

        return 0;
    }

    private static bool TryPublish(Zlink.Socket server, ReadOnlySpan<byte> payload)
    {
        return server.TrySend(payload, SendFlags.DontWait, out int written)
            && written > 0;
    }

    private static long RunPublishPhase(Zlink.Socket server,
        IReadOnlyList<Zlink.Socket> pollSockets, byte[] payload, uint runId,
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
                if (PollSocketWriteReady(pollSockets, pollTimeoutMs) > 0
                    && IsSocketWriteReady(0))
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
                _ = PollSocketWriteReady(pollSockets, timeoutMs);
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
