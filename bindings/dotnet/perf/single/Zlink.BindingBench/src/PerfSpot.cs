using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfSpot
{
    private const string Topic = "bench";
    private const uint RunId = 1;
    private const uint ReadyPhase = 0;
    private const uint ActivePhase = 1;
    private static readonly RoutingId PubNodeRoutingId =
        RoutingId.FromBytes("z-perf-spot-pub"u8);
    private static readonly RoutingId SubNodeRoutingId =
        RoutingId.FromBytes("a-perf-spot-sub"u8);
    private static readonly RoutingId PubSpotRoutingId =
        RoutingId.FromBytes("z-perf-spot-pub-s"u8);
    private static readonly RoutingId SubSpotRoutingId =
        RoutingId.FromBytes("a-perf-spot-sub-s"u8);

    internal static int RunSpot(string transport, int size)
    {
        if (!IsSupportedTransport(transport))
        {
            PrintUnsupported("SPOT", transport, size,
                "spot_transport_not_supported");
            return 0;
        }

        try
        {
            int durationSeconds = ResolveSingleDurationSeconds();
            int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
            int readySettleMs = ResolveSingleSpotReadySettleMs();
            int latencySampleCap = ResolveSingleLatencyCount("SPOT");

            using var ctx = new Context();
            ApplySingleContextOptions(ctx);
            using var pubNode = new SpotNode(ctx);
            using var subNode = new SpotNode(ctx);
            using var spotPub = pubNode.CreateSpot();
            using var spotSub = subNode.CreateSpot();

            pubNode.SetRoutingId(PubNodeRoutingId);
            subNode.SetRoutingId(SubNodeRoutingId);
            spotPub.SetRoutingId(PubSpotRoutingId);
            spotSub.SetRoutingId(SubSpotRoutingId);
            ConfigureSpotNodeTlsIfNeeded(pubNode, transport);
            ConfigureSpotNodeTlsIfNeeded(subNode, transport);

            string publisherEndpoint = EndpointFor(transport, "spot-publisher");
            pubNode.Bind(publisherEndpoint);
            subNode.ConnectPeer(publisherEndpoint);
            spotSub.SetSubscription(Topic);

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var probePayload = new byte[payloadSize];
            var activePayload = new byte[payloadSize];
            Array.Fill(probePayload, (byte)'p');
            Array.Fill(activePayload, (byte)'a');

            if (!WaitForSubscriptionReady(spotPub, spotSub, probePayload, size))
            {
                Console.Error.WriteLine("single_spot_error:subscription_not_ready");
                return 2;
            }

            if (readySettleMs > 0)
                Thread.Sleep(readySettleMs);

            if (!RunActiveWindow(spotPub, spotSub, activePayload, size,
                    durationSeconds, recvTimeoutMs, latencySampleCap,
                    out long received, out List<double> latencySamples))
            {
                Console.Error.WriteLine("single_spot_error:active_window_failed");
                return 2;
            }

            if (received <= 0 || latencySamples.Count == 0)
                return 2;

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("SPOT", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"single_spot_error:exception:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
    }

    private static bool WaitForSubscriptionReady(Spot publisher,
        Spot subscriber, byte[] probePayload, int msgSize)
    {
        ulong seq = 1;
        long deadlineTicks = DeadlineTicksFromMilliseconds(
            ResolveSpotReadyTimeoutMs());
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (!PublishMetricPayload(publisher, probePayload, msgSize, seq++,
                    ReadyPhase, out _))
                return false;

            long probeDeadlineTicks = Math.Min(deadlineTicks,
                DeadlineTicksFromMilliseconds(50));
            while (Stopwatch.GetTimestamp() < probeDeadlineTicks)
            {
                int recvRc = ReceiveSpotHeader(subscriber, msgSize,
                    out var header, out bool headerOk);
                if (recvRc > 0)
                {
                    if (headerOk && IsExpectedSingleHeader(header, msgSize,
                            ReadyPhase, RunId))
                        return true;
                    continue;
                }

                if (recvRc < 0)
                    return false;

                Thread.Yield();
            }
        }

        return false;
    }

    private static bool RunActiveWindow(Spot publisher, Spot subscriber,
        byte[] payload, int msgSize, int durationSeconds, int recvTimeoutMs,
        int latencySampleCap, out long received,
        out List<double> latencySamples)
    {
        _ = recvTimeoutMs;
        long activeDeadlineTicks = DeadlineTicksFromSeconds(
            Math.Max(1, durationSeconds));
        var samples = new List<double>(Math.Max(0, latencySampleCap));
        long activeReceived = 0;
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        int ok = 1;

        // PERF_SINGLE_TEST_POLICY § 1.4: receiver exits on wire-level
        // stop token instead of `senderDone` + drain timer.
        var receiverThread = new Thread(() =>
        {
            while (true)
            {
                int recvRc = ReceiveSpotPayload(subscriber, msgSize,
                    out PerfMetricHeader header, out bool headerOk,
                    out bool isStopToken);
                if (recvRc > 0)
                {
                    if (isStopToken)
                        return;

                    long nowTicks = Stopwatch.GetTimestamp();
                    if (headerOk && IsExpectedSingleHeader(header, msgSize,
                            ActivePhase, RunId)
                        && nowTicks < activeDeadlineTicks)
                    {
                        activeReceived++;
                        ulong nowNs = EpochNs();
                        if (nowNs >= header.SentTsNs)
                        {
                            ReservoirSample(samples, nowNs - header.SentTsNs,
                                ref sampleSeen, latencySampleCap, ref rng);
                        }
                    }

                    continue;
                }

                if (recvRc == 0)
                {
                    Thread.Yield();
                    continue;
                }

                Volatile.Write(ref ok, 0);
                return;
            }
        })
        {
            IsBackground = true
        };

        receiverThread.Start();

        ulong seq = 1;
        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            if (!PublishMetricPayload(publisher, payload, msgSize, seq,
                    ActivePhase, out bool sent))
            {
                Volatile.Write(ref ok, 0);
                break;
            }

            if (sent)
                seq++;
        }

        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
        // stop token published on the same topic.
        PublishSpotStopTokenBlocking(publisher, Topic, "[single-spot]");
        receiverThread.Join();

        received = activeReceived;
        latencySamples = samples;
        return Volatile.Read(ref ok) != 0;
    }

    private static bool PublishMetricPayload(Spot publisher, byte[] payload,
        int msgSize, ulong seq, uint phase, out bool sent)
    {
        sent = false;
        StampMetricHeader(payload.AsSpan(), RunId, phase, msgSize, seq,
            EpochNs());
        using var message = new Message(payload.AsSpan());
        try
        {
            sent = publisher.Publish(Topic)
                .Message(message)
                .Flags(SendFlags.DontWait)
                .Submit();
            return true;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno)
                                        || PerfShared.IsTransientNetworkError(
                                            ex.InternalErrno))
        {
            return true;
        }
    }

    private static int ReceiveSpotHeader(Spot subscriber, int msgSize,
        out PerfMetricHeader header, out bool headerOk)
    {
        return ReceiveSpotPayload(subscriber, msgSize, out header, out headerOk,
            out _);
    }

    private static int ReceiveSpotPayload(Spot subscriber, int msgSize,
        out PerfMetricHeader header, out bool headerOk, out bool isStopToken)
    {
        header = default;
        headerOk = false;
        isStopToken = false;
        TopicMessage? subscribed;
        try
        {
            subscribed = subscriber.Subscribe(RecvFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno))
        {
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-spot] receive failed: {ex.Message}");
            return -1;
        }

        if (subscribed == null)
            return 0;

        using (subscribed)
        {
            if (!string.Equals(subscribed.Topic, Topic, StringComparison.Ordinal)
                || !subscribed.IsSinglePart)
                return 1;

            Message body = subscribed.FirstPart();
            ReadOnlySpan<byte> payload = body.AsReadOnlySpan();
            if (StopToken.IsStopToken(payload))
            {
                isStopToken = true;
                return 1;
            }
            headerOk = TryDecodeMetricHeader(payload, out header)
                && header.MsgSize == (uint)msgSize;
            return 1;
        }
    }

    private static bool IsSupportedTransport(string transport)
    {
        return string.Equals(transport, "tcp", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "tls", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "ws", StringComparison.OrdinalIgnoreCase)
            || string.Equals(transport, "wss", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsWouldBlock(int errno)
    {
        return PerfShared.IsWouldBlock(errno);
    }

    private static bool IsInterrupted(int errno)
    {
        return PerfShared.IsInterrupted(errno);
    }
}
