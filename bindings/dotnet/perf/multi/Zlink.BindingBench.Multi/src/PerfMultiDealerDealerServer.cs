using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-dealer-dealer", options);

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new DealerSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        const uint expectedRunId = 1;
        long recvCount = 0;
        if (!RunReceivePhase(pollManager, server, size, expectedRunId,
                PerfPhase.Active, Math.Max(1, durationSeconds), true, true,
                latSamples, ref sampleSeen, ref rng, ref recvCount))
            return 2;

        if (recvCount <= 0)
            return 2;

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = recvCount / configuredSeconds;
        var latency = ComputeLatencyStats(latSamples);
        double fallbackLatencyNs = (configuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, recvCount);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;
        PrintResult(options.Pattern, options.Transport, size, throughput, latencyNs,
            latencyP95Ns, latencyP99Ns);
        return 0;
    }

    private static bool RunReceivePhase(PollManager pollManager,
        DealerSocket server,
        int msgSize, uint expectedRunId, PerfPhase expectedPhase,
        double durationSeconds, bool countMessage, bool collectLatency,
        List<double> latSamples, ref long sampleSeen, ref uint rng,
        ref long recvCount)
    {
        if (durationSeconds <= 0.0)
            return true;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)(durationSeconds * Stopwatch.Frequency);
        var sockets = new[] { (SocketBase)server };
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int timeoutMs = RemainingMilliseconds(deadlineTicks);
            if (timeoutMs <= 0)
                break;

            if (PollSocketReadReady(pollManager, sockets, timeoutMs) <= 0
                || !IsSocketReadReady(pollManager, 0))
            {
                continue;
            }

            if (!DrainReadableSocket(server, deadlineTicks, msgSize,
                    expectedRunId, expectedPhase, countMessage, collectLatency,
                    latSamples, ref sampleSeen, ref rng, ref recvCount))
                return false;
        }

        return true;
    }

    private static bool DrainReadableSocket(DealerSocket server,
        long deadlineTicks, int msgSize, uint expectedRunId,
        PerfPhase expectedPhase,
        bool countMessage, bool collectLatency, List<double> latSamples,
        ref long sampleSeen, ref uint rng, ref long recvCount)
    {
        while (true)
        {
            Received? received = TryRecvNoWait(server);
            if (received == null)
                return true;

            using (received)
            {
                ReadOnlySpan<byte> body = received.FirstPart().AsReadOnlySpan();
                long recvTicks = Stopwatch.GetTimestamp();
                if (!HandleReceived(body, recvTicks, deadlineTicks, msgSize,
                        expectedRunId, expectedPhase, countMessage,
                        collectLatency, latSamples, ref sampleSeen, ref rng,
                        ref recvCount))
                {
                    return false;
                }
            }
        }
    }

    private static bool HandleReceived(ReadOnlySpan<byte> body, long recvTicks,
        long deadlineTicks, int msgSize, uint expectedRunId,
        PerfPhase expectedPhase, bool countMessage,
        bool collectLatency, List<double> latSamples, ref long sampleSeen,
        ref uint rng, ref long recvCount)
    {
        if (IsStopTokenPayload(body))
            return true;
        if (!PerfRunner.TryDecodeMetricHeader(body, out PerfMetricHeader header))
            return true;
        if (header.RunId != expectedRunId
            || header.MsgSize != (uint)msgSize
            || header.Phase != (uint)expectedPhase)
            return true;
        if (recvTicks > deadlineTicks)
            return true;

        if (countMessage)
            recvCount++;
        if (collectLatency)
        {
            ulong nowNs = EpochNs();
            if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
            {
                double sampleNs = nowNs - header.SentTsNs;
                ReservoirSample(latSamples, sampleNs, ref sampleSeen,
                    latSamples.Capacity, ref rng);
            }
        }

        return true;
    }

    private static Received? TryRecvNoWait(DealerSocket socket)
    {
        return socket.TryRecv(out Received? received) ? received : null;
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
