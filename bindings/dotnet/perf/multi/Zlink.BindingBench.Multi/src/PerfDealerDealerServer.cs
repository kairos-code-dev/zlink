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
        string endpoint = MultiEndpointFor(transport, "multi-dealer-dealer");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Dealer);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);

        using var monitor = server.MonitorOpen(
            SocketEvent.ConnectionReady
            | SocketEvent.Accepted
            | SocketEvent.Connected);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitMonitorReady(monitor, readyTimeoutMs, true))
            return 2;

        var payload = new byte[Math.Max(256, Math.Max(Math.Max(size,
            PerfMetricHeaderSize), MultiStopToken.Length))];
        long recvCount = 0;
        long benchStartTicks = 0;
        long benchEndTicks = 0;
        long hardStopTicks = 0;
        int settleSeconds = (settleMs + 999) / 1000;
        long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(6, warmupSeconds + settleSeconds + 3)
            * Stopwatch.Frequency;
        long lastActiveMessageTicks = 0;
        long idleBreakTicks = (long)(Stopwatch.Frequency
            * (Math.Max(rcvTimeoutMs * 2, 1000) / 1000.0));
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        const uint expectedRunId = 1;
        using var poller = new Poller();
        var events = new List<PollEvent>(1);
        poller.Add(server, PollEvents.PollIn);

        while (true)
        {
            if (hardStopTicks > 0 && Stopwatch.GetTimestamp() >= hardStopTicks)
                break;

            if (!WaitForEvents(poller, events, 2))
            {
                long nowTicks = Stopwatch.GetTimestamp();
                if (lastActiveMessageTicks > 0)
                {
                    if (nowTicks - lastActiveMessageTicks >= idleBreakTicks)
                        break;
                }
                else if (nowTicks >= firstPacketDeadlineTicks)
                {
                    break;
                }
                continue;
            }

            bool handled = false;
            while (true)
            {
                int n = ReceiveRetry(server, payload.AsSpan(), ReceiveFlags.DontWait);
                if (n <= 0)
                    break;

                handled = true;
                ReadOnlySpan<byte> body = payload.AsSpan(0, n);
                if (IsStopTokenPayload(body))
                    goto Done;
                if (!TryDecodeMetricHeader(body, out PerfMetricHeader header))
                    continue;
                if (header.RunId != expectedRunId
                    || header.MsgSize != (uint)size
                    || header.Phase != (uint)PerfPhase.Active)
                    continue;

                if (benchStartTicks == 0)
                {
                    benchStartTicks = Stopwatch.GetTimestamp();
                    hardStopTicks = benchStartTicks
                        + (long)Math.Max(2, durationSeconds + 2)
                        * Stopwatch.Frequency;
                }
                benchEndTicks = Stopwatch.GetTimestamp();
                lastActiveMessageTicks = benchEndTicks;
                recvCount++;
                ulong nowUs = EpochUs();
                if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                {
                    double sampleUs = nowUs - header.SentTsUs;
                    ReservoirSample(latSamples, sampleUs, ref sampleSeen,
                        latencySampleCap, ref rng);
                }

                if (hardStopTicks > 0
                    && Stopwatch.GetTimestamp() >= hardStopTicks)
                {
                    goto Done;
                }
            }

            if (handled)
                continue;
        }

Done:
        if (benchStartTicks > 0 && recvCount > 0)
        {
            double elapsedSeconds = (benchEndTicks - benchStartTicks)
                / (double)Stopwatch.Frequency;
            double throughput = elapsedSeconds > 0.0
                ? recvCount / elapsedSeconds
                : 0.0;
            var latency = ComputeLatencyStats(latSamples);
            double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
                / Math.Max(1.0, recvCount);
            double latencyUs = latency.mean > 0.0 ? latency.mean
                : fallbackLatencyUs;
            double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
            double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
            PrintResult(pattern, transport, size, throughput, latencyUs,
                latencyP95Us, latencyP99Us);
        }

        return 0;
    }
}
