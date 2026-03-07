using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouterServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "ROUTER_ROUTER";
        size = Math.Max(1, size);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int latencySampleCap = ResolveMultiLatencySampleCap();
        string endpoint = MultiEndpointFor(transport, "multi-router-router");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Router);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RoutingId, "SERVER");

        using var monitor = server.MonitorOpen(
            SocketEvent.ConnectionReady
            | SocketEvent.Accepted
            | SocketEvent.Connected);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitMonitorReady(monitor, readyTimeoutMs, true))
            return 2;

        var routingId = new byte[256];
        var payload = new byte[Math.Max(256, Math.Max(Math.Max(size,
            PerfMetricHeaderSize), MultiStopToken.Length))];
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

        while (true)
        {
            if (!WaitForEvents(poller, events, 2))
                continue;

            while (TryReceiveRouterMessage(server, routingId, payload,
                       out int ridLen, out int n))
            {
                ReadOnlySpan<byte> body = payload.AsSpan(0, n);
                if (IsStopTokenPayload(body))
                    goto Done;

                if (TryDecodeMetricHeader(body, out PerfMetricHeader header)
                    && header.RunId == expectedRunId
                    && header.MsgSize == (uint)size
                    && header.Phase == (uint)PerfPhase.Active)
                {
                    if (benchStartTicks == 0)
                        benchStartTicks = Stopwatch.GetTimestamp();
                    benchEndTicks = Stopwatch.GetTimestamp();
                    echoCount++;
                    ulong nowUs = EpochUs();
                    if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                    {
                        double sampleUs = nowUs - header.SentTsUs;
                        ReservoirSample(latSamples, sampleUs, ref sampleSeen,
                            latencySampleCap, ref rng);
                    }
                }

                int sentRid = SendBlocking(server, routingId.AsSpan(0, ridLen),
                    SendFlags.SendMore);
                if (sentRid <= 0)
                    continue;

                _ = SendBlocking(server, body, SendFlags.None);
            }
        }

Done:
        if (benchStartTicks > 0 && echoCount > 0)
        {
            double elapsedSeconds = (benchEndTicks - benchStartTicks)
                / (double)Stopwatch.Frequency;
            double throughput = elapsedSeconds > 0.0
                ? echoCount / elapsedSeconds
                : 0.0;
            var latency = ComputeLatencyStats(latSamples);
            double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
                / Math.Max(1.0, echoCount);
            double latencyUs = latency.mean > 0.0 ? latency.mean
                : fallbackLatencyUs;
            double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
            double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
            PrintResult(pattern, transport, size, throughput, latencyUs,
                latencyP95Us, latencyP99Us);
        }

        return 0;
    }

    private static bool TryReceiveRouterMessage(Zlink.Socket server,
        byte[] routingId, byte[] payload, out int routingIdLength,
        out int payloadLength)
    {
        routingIdLength = ReceiveRetry(server, routingId.AsSpan(),
            ReceiveFlags.DontWait);
        if (routingIdLength <= 0)
        {
            payloadLength = 0;
            return false;
        }

        payloadLength = ReceiveRetry(server, payload.AsSpan(),
            ReceiveFlags.DontWait);
        return payloadLength > 0;
    }
}
