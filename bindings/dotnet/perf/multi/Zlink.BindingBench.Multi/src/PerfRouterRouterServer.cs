using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouterServer
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
            "multi-router-router", options);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new RouterSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.RouterOptions.RoutingId = RoutingId.FromBytes("SERVER"u8);
        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady
            | SocketEvent.Connected | SocketEvent.Accepted);

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
            if (!WaitForEvents(poller, events, PollTimeoutMs))
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
                    Message[] replies = CreateFreshParts(received.Parts);
                    try
                    {
                        server.Send(received.RoutingId.Value, replies);
                    }
                    finally
                    {
                        DisposeAllQuietly(replies);
                    }
                }
                catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                                || IsInterrupted(ex.InternalErrno))
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

    private static void DisposeReceived(Received? received)
    {
        if (received == null)
            return;

        for (int i = 0; i < received.Parts.Count; i++)
            TryDisposeQuietly(received.Parts[i]);
    }

    private static Message[] CreateFreshParts(IReadOnlyList<Message> parts)
    {
        var fresh = new Message[parts.Count];
        try
        {
            for (int i = 0; i < parts.Count; i++)
                fresh[i] = Message.FromBytes(parts[i].AsReadOnlySpan());
            return fresh;
        }
        catch
        {
            for (int i = 0; i < fresh.Length; i++)
                TryDisposeQuietly(fresh[i]);
            throw;
        }
    }
}
