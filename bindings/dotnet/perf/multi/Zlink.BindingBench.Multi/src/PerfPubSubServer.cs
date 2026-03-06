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
        const int subscribeSettleMs = 300;
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        if (size >= 65536 && warmupSeconds > 0)
            warmupSeconds = 0;
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int sndTimeoutMs = ResolveMultiSndTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        string endpoint = MultiEndpointFor(transport, "multi-pubsub");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Pub);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);

        using var monitor = server.MonitorOpen(
            SocketEvent.ConnectionReady
            | SocketEvent.Accepted
            | SocketEvent.Connected);

        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitMonitorReady(monitor, readyTimeoutMs, true))
            return 2;

        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        Thread.Sleep(subscribeSettleMs);

        if (warmupSeconds > 0)
        {
            long warmupDeadline = Stopwatch.GetTimestamp()
                + (long)warmupSeconds * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < warmupDeadline)
            {
                StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Warmup,
                    size, seq++, EpochUs());
                _ = TryPublish(server, payload.AsSpan());
            }
        }

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        long sendCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active, size,
                seq++, EpochUs());
            if (TryPublish(server, payload.AsSpan()))
                sendCount++;
        }
        long benchEndTicks = Stopwatch.GetTimestamp();

        double elapsedSeconds = (benchEndTicks - benchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? sendCount / elapsedSeconds
            : 0.0;
        double latencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, sendCount);
        PrintResult(pattern, transport, size, throughput, latencyUs,
            latencyUs, latencyUs);

        return 0;
    }

    private static bool TryPublish(Zlink.Socket server, ReadOnlySpan<byte> payload)
    {
        return server.Send(payload, SendFlags.None) > 0;
    }
}
