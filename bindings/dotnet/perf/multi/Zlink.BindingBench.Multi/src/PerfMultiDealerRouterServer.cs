using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfMultiDealerRouterServer
{
    private const int PollTimeoutMs = 50;

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-dealer-router", options);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new RouterSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        WriteStdoutLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
            return 2;

        long echoCount = 0;
        long benchStartTicks = 0;
        long benchEndTicks = 0;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        const uint expectedRunId = 1;
        using var poller = new Poller();
        var events = new List<PollEvent>(1);
        poller.Add(server, PollEvents.PollIn);

        bool stop = false;
        while (!stop)
        {
            if (!WaitForEvents(poller, events, PollTimeoutMs))
                continue;
            if ((events[0].Revents & PollEvents.PollIn) == 0)
                continue;

            while (true)
            {
                using Received? received = TryRecvNoWait(server);
                if (received == null || received.Count == 0)
                    break;

                Message bodyMessage = received.SinglePartOrThrow();
                ReadOnlySpan<byte> body = bodyMessage.AsReadOnlySpan();
                if (IsStopTokenPayload(body))
                {
                    stop = true;
                    break;
                }

                if (TryDecodeMetricHeader(body, out PerfMetricHeader header)
                    && header.RunId == expectedRunId
                    && header.MsgSize == (uint)size
                    && header.Phase == (uint)PerfPhase.Active)
                {
                    if (benchStartTicks == 0)
                        benchStartTicks = Stopwatch.GetTimestamp();
                    benchEndTicks = Stopwatch.GetTimestamp();
                    echoCount++;
                    ulong nowNs = EpochNs();
                    if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
                    {
                        double sampleNs = nowNs - header.SentTsNs;
                        ReservoirSample(latSamples, sampleNs, ref sampleSeen,
                            latencySampleCap, ref rng);
                    }
                }

                if (received.RoutingId == null)
                    return 2;
                using Message reply = bodyMessage.Move();
                server.Send(received.RoutingId.Value, reply);
            }
        }
        if (benchStartTicks > 0 && echoCount > 0)
        {
            double configuredSeconds = Math.Max(1.0,
                ResolveMultiDurationSeconds(options));
            double throughput = echoCount / configuredSeconds;
            var latency = ComputeLatencyStats(latSamples);
            double fallbackLatencyNs = (configuredSeconds * 1_000_000_000.0)
                / Math.Max(1.0, echoCount);
            double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
            double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
            double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;
            PrintResult(options.Pattern, options.Transport, size, throughput, latencyNs,
                latencyP95Ns, latencyP99Ns);
        }

        return 0;
    }

    private static Received? TryRecvNoWait(RouterSocket socket)
    {
        return socket.TryRecv(out Received? received) ? received : null;
    }

}
