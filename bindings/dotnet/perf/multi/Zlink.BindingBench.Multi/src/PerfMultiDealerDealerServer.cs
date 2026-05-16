using System;
using System.Collections.Generic;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiDealerDealerServer
{
    private const int ServerSocketTag = 0;
    private const int ActiveDeadlineTag = -1;

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
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
        endpoint = server.Options.LastEndpoint;
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

        var result = RunReceivePhase(server, size, latencySampleCap,
            durationSeconds);
        if (result.measureCount <= 0)
            return 2;

        PrintResult(options.Pattern, options.Transport, size, result.throughput,
            result.latencyNs, result.latencyP95Ns, result.latencyP99Ns);
        return 0;
    }

    private static (double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns, long measureCount)
        RunReceivePhase(DealerSocket server, int msgSize, int latencySampleCap,
            int durationSeconds)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long measureCount = 0;
        using var receivedBuffer = new Received();

        // PERF_MULTI_TEST_POLICY: the active window ends purely on the
        // configured duration (signal-driven -1 poll, duration timer), like C
        // perf_multi_dealer_dealer_server.cpp run_receive_window. C's
        // subsequent drain_phase_until_idle exists ONLY because the C multi
        // server process is reused across every size case; the .NET multi
        // runner (multi/run_benchmarks.sh) spawns and tears down a fresh
        // server process per size (for size loop, server start + STOP/wait
        // each iteration), so there is no cross-size backlog to drain. The
        // process exits after this size, discarding any tail like C discards
        // it across runs. No idle-drain phase is performed.
        if (!ReceiveActiveWindow(server, receivedBuffer, msgSize, expectedRunId,
                PerfPhase.Active, latSamples, ref sampleSeen, ref rng,
                ref measureCount, durationSeconds))
        {
            return (0.0, 0.0, 0.0, 0.0, 0);
        }

        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / configuredSeconds;
        // PERF_POLICY: report measured latency only. C
        // normalize_latency_stats reports zeros when no samples and never
        // fabricates a duration-derived latency.
        var latency = ComputeLatencyStats(latSamples);
        double latencyNs = latency.mean;
        double latencyP95Ns = Math.Max(latency.p95, latencyNs);
        double latencyP99Ns = Math.Max(latency.p99, latencyP95Ns);

        return (throughput, latencyNs, latencyP95Ns, latencyP99Ns, measureCount);
    }

    private static bool ReceiveActiveWindow(DealerSocket server,
        Received receivedBuffer, int msgSize, uint expectedRunId,
        PerfPhase expectedPhase, List<double> latSamples, ref long sampleSeen,
        ref uint rng, ref long messageCount, int durationSeconds)
    {
        using var poller = new Poller();
        using var activeTimer = new Systems.Zlink.Timer();
        var events = new PollEvent[2];
        poller.Add(server, PollEventFlags.PollIn, ServerSocketTag);
        poller.Add(activeTimer, ActiveDeadlineTag);
        activeTimer.Start(TimeSpan.FromSeconds(Math.Max(1, durationSeconds)),
            1);

        while (true)
        {
            int written = poller.Wait(events,
                TimeSpan.FromMilliseconds(-1), out _);

            for (int i = 0; i < written; i++)
            {
                if (events[i].Tag is ActiveDeadlineTag)
                {
                    _ = events[i].Timer?.Recv();
                    poller.Remove(activeTimer);
                    return true;
                }

                if (events[i].Tag is not int tag
                    || tag != ServerSocketTag
                    || (events[i].Revents & PollEventFlags.PollIn) == 0)
                    continue;

                DrainAvailable(server, receivedBuffer, msgSize, expectedRunId,
                    expectedPhase, latSamples, ref sampleSeen, ref rng,
                    ref messageCount, collectMetrics: true);
            }
        }
    }

    private static int DrainAvailable(DealerSocket server, Received receivedBuffer,
        int msgSize, uint expectedRunId, PerfPhase expectedPhase,
        List<double> latSamples, ref long sampleSeen, ref uint rng,
        ref long messageCount, bool collectMetrics)
    {
        int drained = 0;
        while (true)
        {
            if (!TryRecvNoWait(server, receivedBuffer))
                return drained;

            drained++;
            ReadOnlySpan<byte> body = receivedBuffer.SinglePartOrThrow()
                .AsReadOnlySpan();
            if (IsStopTokenPayload(body))
                continue;

            if (!PerfShared.TryDecodeMetricHeader(body, out PerfMetricHeader header)
                || header.RunId != expectedRunId
                || header.MsgSize != (uint)msgSize
                || header.Phase != (uint)expectedPhase)
                continue;

            if (!collectMetrics)
                continue;

            messageCount++;
            if (header.SentTsNs > 0)
            {
                ulong nowNs = EpochNs();
                if (nowNs >= header.SentTsNs)
                {
                    double sampleLatencyNs = nowNs - header.SentTsNs;
                    ReservoirSample(latSamples, sampleLatencyNs, ref sampleSeen,
                        latSamples.Capacity, ref rng);
                }
            }
        }
    }

    private static bool TryRecvNoWait(DealerSocket socket, Received result)
    {
        return socket.Recv(result, RecvFlags.DontWait);
    }
}
