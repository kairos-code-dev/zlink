using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouterServer
{
    private enum RouterSendStage
    {
        None = 0,
        RoutingId = 1,
        Payload = 2,
    }

    private sealed class PendingRouterMessage
    {
        internal PendingRouterMessage(byte[] routingId, int routingIdLength,
            byte[] payload, int payloadLength)
        {
            RoutingId = routingId;
            RoutingIdLength = routingIdLength;
            Payload = payload;
            PayloadLength = payloadLength;
            Stage = RouterSendStage.RoutingId;
        }

        internal byte[] RoutingId { get; }
        internal int RoutingIdLength { get; }
        internal byte[] Payload { get; }
        internal int PayloadLength { get; }
        internal RouterSendStage Stage { get; set; }
    }

    internal static int Run(string transport, int size)
    {
        const string pattern = "ROUTER_ROUTER";
        size = Math.Max(1, size);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int latencySampleCap = ResolveMultiLatencySampleCap();
        int clientCount = ResolveMultiClients(pattern);
        string endpoint = MultiEndpointFor(transport, "multi-router-router");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Router);
        ApplyMultiSocketOptions(server, pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RoutingId, "SERVER");

        using var monitor = server.MonitorOpen(SocketEvent.ConnectionReady);

        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        if (!WaitConnectReadyCount(monitor, clientCount, readyTimeoutMs))
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
        var pending = new List<PendingRouterMessage>(Math.Max(64, clientCount * 2));
        poller.Add(server, PollEvents.PollIn);

        while (true)
        {
            poller.Modify(server, pending.Count > 0
                ? PollEvents.PollIn | PollEvents.PollOut
                : PollEvents.PollIn);

            if (!WaitForEvents(poller, events, 50))
                continue;

            if ((events[0].Revents & PollEvents.PollOut) != 0
                && !FlushPending(server, pending))
            {
                return 2;
            }

            if ((events[0].Revents & PollEvents.PollIn) == 0)
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

                if (!TrySendRouterMessage(server, routingId, ridLen, payload, n,
                        pending))
                {
                    return 2;
                }
            }
        }

Done:
        if (benchStartTicks > 0 && echoCount > 0)
        {
            double elapsedSeconds = (benchEndTicks - benchStartTicks)
                / (double)Stopwatch.Frequency;
            double configuredSeconds = Math.Max(1.0, ResolveMultiDurationSeconds());
            double throughput = echoCount / configuredSeconds;
            var latency = ComputeLatencyStats(latSamples);
            double fallbackLatencyUs = (configuredSeconds * 1_000_000.0)
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

    private static bool FlushPending(Zlink.Socket server,
        List<PendingRouterMessage> pending)
    {
        int index = 0;
        while (index < pending.Count)
        {
            if (TryFlushPending(server, pending[index]))
            {
                pending.RemoveAt(index);
                continue;
            }

            index++;
        }

        return true;
    }

    private static bool TrySendRouterMessage(Zlink.Socket server,
        byte[] routingId, int routingIdLength, byte[] payload, int payloadLength,
        List<PendingRouterMessage> pending)
    {
        if (server.TrySend(routingId.AsSpan(0, routingIdLength),
                SendFlags.SendMore | SendFlags.DontWait, out int ridWritten)
            && ridWritten > 0)
        {
            if (server.TrySend(payload.AsSpan(0, payloadLength),
                    SendFlags.DontWait, out int payloadWritten)
                && payloadWritten > 0)
            {
                return true;
            }

            pending.Add(new PendingRouterMessage(Array.Empty<byte>(), 0,
                CopyFrame(payload, payloadLength), payloadLength)
            {
                Stage = RouterSendStage.Payload,
            });
            return true;
        }

        pending.Add(new PendingRouterMessage(
            CopyFrame(routingId, routingIdLength),
            routingIdLength,
            CopyFrame(payload, payloadLength),
            payloadLength));
        return true;
    }

    private static bool TryFlushPending(Zlink.Socket server,
        PendingRouterMessage message)
    {
        if (message.Stage == RouterSendStage.RoutingId)
        {
            if (!server.TrySend(message.RoutingId.AsSpan(0, message.RoutingIdLength),
                    SendFlags.SendMore | SendFlags.DontWait, out int ridWritten)
                || ridWritten <= 0)
            {
                return false;
            }

            message.Stage = RouterSendStage.Payload;
        }

        if (message.Stage == RouterSendStage.Payload)
        {
            if (!server.TrySend(message.Payload.AsSpan(0, message.PayloadLength),
                    SendFlags.DontWait, out int payloadWritten)
                || payloadWritten <= 0)
            {
                return false;
            }

            message.Stage = RouterSendStage.None;
        }

        return true;
    }

    private static byte[] CopyFrame(byte[] source, int length)
    {
        var copy = new byte[Math.Max(1, length)];
        source.AsSpan(0, Math.Max(0, length)).CopyTo(copy);
        return copy;
    }
}
