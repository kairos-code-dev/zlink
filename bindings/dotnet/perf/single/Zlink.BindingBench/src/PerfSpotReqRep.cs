using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfSpotReqRep
{
    private const string ChannelName = "perf.spot.reqrep";
    private const uint RunId = 1;
    private const uint WarmupPhase = 0;
    private const uint ActivePhase = 1;

    internal static int Run(string transport, int size)
    {
        if (!IsSupportedTransport(transport))
        {
            PrintUnsupported("SPOT_REQREP", transport, size,
                "spot_reqrep_transport_not_supported");
            return 0;
        }

        int durationSeconds = ResolveSingleDurationSeconds();
        int readyTimeoutMs = ResolveSpotReadyTimeoutMs();
        int latencySampleCap = ResolveSingleLatencyCount("SPOT_REQREP");
        string bindEndpoint = EndpointFor(transport, "single-spot-reqrep");
        string connectEndpoint = NormalizeClientEndpoint(bindEndpoint, transport);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var requesterNode = new SpotNode(ctx);
        using var requesterDealer = new DealerSocket(ctx);
        using var requester = requesterNode.CreateSpot();
        using var responder = new RouterSocket(ctx);

        try
        {
            ApplySingleSocketOptions(requesterDealer);
            ApplySingleSocketOptions(responder);
            ConfigureTlsClientIfNeeded(requesterDealer, transport);
            ConfigureTlsServerIfNeeded(responder, transport);
            requesterNode.AttachChannelDealerManual(ChannelName, requesterDealer);
            responder.Bind(bindEndpoint);
            requesterDealer.Connect(connectEndpoint);

            Exception? responderFailure = null;
            // PERF_SINGLE_TEST_POLICY § 1.4: wire-level stop token signals
            // the responder to exit its echo loop. The token is sent as
            // the requester's final request so the requester also drains
            // the matching reply via public poller completion.
            var responderThread = new Thread(() =>
            {
                try
                {
                    while (true)
                    {
                        using Received received = responder.Recv();
                        RoutingId routingId = received.RoutingId
                            ?? throw new InvalidOperationException("missing routing id");
                        ulong requestSeq = received.RequestSeq ?? 0UL;
                        ReadOnlySpan<byte> body = received.FirstPart().AsReadOnlySpan();
                        bool isStop = StopToken.IsStopToken(body);

                        using Message reply = received.FirstPart().Move();
                        responder.Reply(routingId, requestSeq, reply);

                        if (isStop)
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref responderFailure, ex, null);
                }
            })
            {
                IsBackground = true,
            };
            responderThread.Start();

            if (!WaitForWarmupReady(requester, size, readyTimeoutMs))
                return 2;

            var samples = new List<double>(Math.Max(0, latencySampleCap));
            long sampleSeen = 0;
            uint rng = 0xA341316Cu;
            long measureCount = 0;
            long activeDeadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
            ulong seq = 1;

            while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
            {
                using var active = Message.FromBytes(
                    CreatePayload(size, ActivePhase, seq++));
                PerfMetricHeader reply = RequestReply(requester, size, active,
                    TimeSpan.FromSeconds(2));
                if (reply.Phase != ActivePhase)
                    continue;

                measureCount++;
                ulong nowNs = EpochNs();
                if (nowNs >= reply.SentTsNs)
                {
                    ReservoirSample(samples, (nowNs - reply.SentTsNs) / 2.0,
                        ref sampleSeen, latencySampleCap, ref rng);
                }
            }

            // Final request carries the wire-level stop token so the
            // responder thread exits its loop after sending the reply.
            using (var stopRequest = Message.FromBytes(StopToken.Bytes))
                _ = RequestReplyAllowStopToken(requester, stopRequest,
                    TimeSpan.FromSeconds(2));

            responderThread.Join();
            if (responderFailure != null)
                throw new InvalidOperationException("spot reqrep responder failed",
                    responderFailure);
            if (measureCount <= 0)
                return 2;

            double throughput = measureCount / (double)Math.Max(1, durationSeconds);
            double fallbackLatencyNs = (Math.Max(1.0, durationSeconds)
                * 1_000_000_000.0) / Math.Max(1.0, measureCount * 2.0);
            var latency = ComputeLatencyStats(samples);
            double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
            double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
            double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;
            PrintResult("SPOT_REQREP", transport, size, throughput, latencyNs,
                latencyP95Ns, latencyP99Ns, bandwidthMultiplier: 2.0);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"single_spot_reqrep_error:exception:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
    }

    private static bool WaitForWarmupReady(Spot requester, int size, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        ulong seq = 1;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                using var probe = Message.FromBytes(
                    CreatePayload(size, WarmupPhase, seq++));
                PerfMetricHeader reply = RequestReply(requester, size, probe,
                    TimeSpan.FromMilliseconds(200));
                if (reply.Phase == WarmupPhase)
                    return true;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[single-spot-reqrep] warmup probe failed: {ex.Message}");
                Thread.Sleep(10);
            }
        }

        return false;
    }

    private static PerfMetricHeader RequestReply(Spot requester, int size,
        Message payload, TimeSpan timeout)
    {
        IReadOnlyList<Message> replyParts = requester.RequestChannel(ChannelName)
            .Message(payload)
            .Timeout(timeout)
            .SubmitAsync()
            .GetAwaiter().GetResult();
        try
        {
            if (replyParts.Count == 0)
                throw new InvalidOperationException("spot reqrep reply was empty");

            ReadOnlySpan<byte> body = replyParts[0].AsReadOnlySpan();
            if (TryDecodeMetricHeader(body, out PerfMetricHeader header)
                && header.MsgSize == (uint)size
                && header.RunId == RunId)
            {
                return header;
            }

            throw new InvalidOperationException("spot reqrep reply header invalid");
        }
        finally
        {
            for (int i = 0; i < replyParts.Count; i++)
                replyParts[i].Dispose();
        }
    }

    private static bool RequestReplyAllowStopToken(Spot requester,
        Message payload, TimeSpan timeout)
    {
        IReadOnlyList<Message> replyParts = requester.RequestChannel(ChannelName)
            .Message(payload)
            .Timeout(timeout)
            .SubmitAsync()
            .GetAwaiter().GetResult();
        try
        {
            return replyParts.Count > 0
                && StopToken.IsStopToken(replyParts[0].AsReadOnlySpan());
        }
        finally
        {
            for (int i = 0; i < replyParts.Count; i++)
                replyParts[i].Dispose();
        }
    }

    private static byte[] CreatePayload(int size, uint phase, ulong seq)
    {
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        StampMetricHeader(payload.AsSpan(), RunId, phase, size, seq, EpochNs());
        return payload;
    }

    private static string NormalizeClientEndpoint(string endpoint, string transport)
    {
        if (!transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            && !transport.Equals("wss", StringComparison.OrdinalIgnoreCase))
        {
            return endpoint;
        }

        return endpoint.Replace("://127.0.0.1:", "://localhost:",
            StringComparison.Ordinal);
    }

    private static bool IsSupportedTransport(string transport)
    {
        return string.Equals(transport, "tcp", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "tls", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "ws", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "wss", StringComparison.OrdinalIgnoreCase);
    }
}
