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
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int latencySampleStride = ResolveMultiOnewayLatencySampleStride();
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
        PrintAutoHwmSnapshot(server, "server", options.Transport, size);

        var result = RunReceivePhase(pollManager, server, size,
            latencySampleCap, latencySampleStride, durationSeconds, clientCount);
        if (result.measureCount <= 0)
            return 2;

        PrintResult(options.Pattern, options.Transport, size, result.throughput,
            result.latencyNs, result.latencyP95Ns, result.latencyP99Ns);
        return 0;
    }

    private static (double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns, long measureCount)
        RunReceivePhase(PollManager pollManager, DealerSocket server,
            int msgSize, int latencySampleCap, int latencySampleStride,
            int durationSeconds, int expectedStops)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;
        using var receivedBuffer = new Received();
        long drainDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(6, durationSeconds + 5) * Stopwatch.Frequency;

        if (!ReceiveUntilStops(server, receivedBuffer, msgSize, expectedRunId,
                PerfPhase.Active, expectedStops, latencySampleStride, latSamples,
                ref sampleSeen, ref rng, ref measureCount,
                countThroughput: true, drainDeadlineTicks))
        {
            return (0.0, 0.0, 0.0, 0.0, 0);
        }

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / configuredSeconds;
        double fallbackLatencyNs = (configuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(latSamples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;

        return (throughput, latencyNs, latencyP95Ns, latencyP99Ns, measureCount);
    }

    private static bool ReceiveUntilStops(DealerSocket server,
        Received receivedBuffer, int msgSize, uint expectedRunId,
        PerfPhase expectedPhase, int expectedStops, int latencySampleStride,
        List<double> latSamples, ref long sampleSeen, ref uint rng,
        ref long messageCount, bool countThroughput, long deadlineTicks)
    {
        int stopCount = 0;
        while (stopCount < expectedStops
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (!TryRecvNoWait(server, receivedBuffer))
            {
                System.Threading.Thread.Yield();
                continue;
            }

            while (true)
            {
                if (Stopwatch.GetTimestamp() >= deadlineTicks)
                    return true;

                ReadOnlySpan<byte> body = receivedBuffer.SinglePartOrThrow()
                    .AsReadOnlySpan();
                if (IsStopTokenPayload(body))
                {
                    stopCount++;
                    continue;
                }

                if (!PerfShared.TryDecodeMetricHeader(body,
                        out PerfMetricHeader header)
                    || header.RunId != expectedRunId
                    || header.MsgSize != (uint)msgSize
                    || header.Phase != (uint)expectedPhase)
                {
                    continue;
                }

                messageCount++;
                if (countThroughput
                    && messageCount % latencySampleStride != 0)
                    continue;
                if (header.SentTsNs > 0)
                {
                    ulong nowNs = EpochNs();
                    if (nowNs >= header.SentTsNs)
                    {
                        double sampleLatencyNs = nowNs - header.SentTsNs;
                        ReservoirSample(latSamples, sampleLatencyNs,
                            ref sampleSeen, latSamples.Capacity, ref rng);
                    }
                }

                if (!TryRecvNoWait(server, receivedBuffer))
                    break;
            }
        }

        return true;
    }

    private static bool TryRecvNoWait(DealerSocket socket, Received result)
    {
        return socket.Recv(result, RecvFlags.DontWait);
    }
}
