using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfDealerDealerServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "DEALER_DEALER";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int latencySampleCap = ResolveMultiLatencySampleCap();
        int clientCount = ResolveMultiClients(pattern);
        string endpoint = MultiEndpointFor(transport, "multi-dealer-dealer");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Dealer);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        var payload = new byte[Math.Max(256, Math.Max(Math.Max(size,
            PerfMetricHeaderSize), MultiStopToken.Length))];
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        const uint expectedRunId = 1;
        if (!RunReceivePhase(server, payload, size, expectedRunId,
                PerfPhase.Warmup, warmupSeconds, false, false,
                latSamples, ref sampleSeen, ref rng))
            return 2;
        if (!RunReceivePhase(server, payload, size, expectedRunId,
                PerfPhase.Drain, settleMs / 1000.0, false, false,
                latSamples, ref sampleSeen, ref rng))
            return 2;

        long recvCount = 0;
        if (!RunReceivePhase(server, payload, size, expectedRunId,
                PerfPhase.Active, Math.Max(1, durationSeconds), true, true,
                latSamples, ref sampleSeen, ref rng, ref recvCount))
            return 2;
        if (recvCount <= 0)
            return 2;

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = recvCount / configuredSeconds;
        var latency = ComputeLatencyStats(latSamples);
        double fallbackLatencyUs = (configuredSeconds * 1_000_000.0)
            / Math.Max(1.0, recvCount);
        double latencyUs = latency.mean > 0.0 ? latency.mean
            : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
        PrintResult(pattern, transport, size, throughput, latencyUs,
            latencyP95Us, latencyP99Us);
        return 0;
    }

    private static bool RunReceivePhase(Zlink.Socket server, byte[] payload,
        int msgSize, uint expectedRunId, PerfPhase expectedPhase,
        double durationSeconds, bool countMessage, bool collectLatency,
        List<double> latSamples, ref long sampleSeen, ref uint rng)
    {
        long recvCount = 0;
        return RunReceivePhase(server, payload, msgSize, expectedRunId,
            expectedPhase, durationSeconds, countMessage, collectLatency,
            latSamples, ref sampleSeen, ref rng, ref recvCount);
    }

    private static bool RunReceivePhase(Zlink.Socket server, byte[] payload,
        int msgSize, uint expectedRunId, PerfPhase expectedPhase,
        double durationSeconds, bool countMessage, bool collectLatency,
        List<double> latSamples, ref long sampleSeen, ref uint rng,
        ref long recvCount)
    {
        if (durationSeconds <= 0.0)
            return true;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)(durationSeconds * Stopwatch.Frequency);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int n = ReceiveRetry(server, payload.AsSpan(), ReceiveFlags.None);
            if (n <= 0)
                continue;
            if (!HandleReceived(payload.AsSpan(0, n), msgSize, expectedRunId,
                    expectedPhase, countMessage, collectLatency, latSamples,
                    ref sampleSeen, ref rng, ref recvCount))
                return false;
            if (!DrainRemainingFramesNonBlocking(server, payload, msgSize,
                    expectedRunId, expectedPhase, countMessage, collectLatency,
                    latSamples, ref sampleSeen, ref rng, ref recvCount))
                return false;
        }

        return true;
    }

    private static bool DrainRemainingFramesNonBlocking(Zlink.Socket server,
        byte[] payload, int msgSize, uint expectedRunId, PerfPhase expectedPhase,
        bool countMessage, bool collectLatency, List<double> latSamples,
        ref long sampleSeen, ref uint rng, ref long recvCount)
    {
        while (true)
        {
            int n = ReceiveRetry(server, payload.AsSpan(), ReceiveFlags.DontWait);
            if (n < 0)
                return false;
            if (n == 0)
                return true;
            if (!HandleReceived(payload.AsSpan(0, n), msgSize, expectedRunId,
                    expectedPhase, countMessage, collectLatency, latSamples,
                    ref sampleSeen, ref rng, ref recvCount))
                return false;
        }
    }

    private static bool HandleReceived(ReadOnlySpan<byte> body, int msgSize,
        uint expectedRunId, PerfPhase expectedPhase, bool countMessage,
        bool collectLatency, List<double> latSamples, ref long sampleSeen,
        ref uint rng, ref long recvCount)
    {
        if (IsStopTokenPayload(body))
            return true;
        if (!TryDecodeMetricHeader(body, out PerfMetricHeader header))
            return true;
        if (header.RunId != expectedRunId
            || header.MsgSize != (uint)msgSize
            || header.Phase != (uint)expectedPhase)
            return true;

        if (countMessage)
            recvCount++;
        if (collectLatency)
        {
            ulong nowUs = EpochUs();
            if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
            {
                double sampleUs = nowUs - header.SentTsUs;
                ReservoirSample(latSamples, sampleUs, ref sampleSeen,
                    latSamples.Capacity, ref rng);
            }
        }

        return true;
    }
}
