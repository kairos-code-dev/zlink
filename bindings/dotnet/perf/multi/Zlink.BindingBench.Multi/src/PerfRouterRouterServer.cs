using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouterServer
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        string endpoint = MultiEndpointFor(options.Transport,
            "multi-router-router", options);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new RouterSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.SetOption(SocketOptions.RoutingId, "SERVER");

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

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

        while (true)
        {
            if (!WaitForEvents(poller, events, 0))
                continue;
            if ((events[0].Revents & PollEvents.PollIn) == 0)
                continue;

            while (true)
            {
                Received? received = null;
                try
                {
                    received = server.TryReceiveRouted();
                    if (received == null || received.Parts.Count == 0)
                        break;

                    Message bodyMessage = received.Parts[0];
                    ReadOnlySpan<byte> body = bodyMessage.AsReadOnlySpan();
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

                    SendResult sendResult = received.Parts.Count == 1
                        ? server.TrySend(received.RoutingId, bodyMessage)
                        : server.TrySend(received.RoutingId, received.Parts);
                    if (sendResult != SendResult.Sent)
                        return 2;
                }
                catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                                || IsInterrupted(ex.Errno))
                {
                    break;
                }
                finally
                {
                    DisposeReceived(received);
                }
            }
        }

Done:
        if (benchStartTicks > 0 && echoCount > 0)
        {
            double configuredSeconds = Math.Max(1.0,
                ResolveMultiDurationSeconds(options));
            double throughput = echoCount / configuredSeconds;
            var latency = ComputeLatencyStats(latSamples);
            double fallbackLatencyUs = (configuredSeconds * 1_000_000.0)
                / Math.Max(1.0, echoCount);
            double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
            double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
            double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
            PrintResult(options.Pattern, options.Transport, size, throughput, latencyUs,
                latencyP95Us, latencyP99Us);
        }

        return 0;
    }

    private static void DisposeReceived(Received? received)
    {
        if (received == null)
            return;

        for (int i = 0; i < received.Parts.Count; i++)
            TryDisposeQuietly(received.Parts[i]);
    }
}
