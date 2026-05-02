using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpot
{
    private const string ServiceName = "bench-svc";
    private const string Topic = "bench";
    private const uint RunId = 1;
    private const uint ReadyPhase = 0;
    private const uint ActivePhase = 1;
    private const uint CooldownPhase = 2;

    internal static int RunSpot(string transport, int size)
    {
        if (!IsSupportedTransport(transport))
        {
            PrintUnsupported("SPOT", transport, size,
                "spot_transport_not_supported");
            return 0;
        }

        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int readySettleMs = ResolveSingleSpotReadySettleMs();
        int latencySampleCap = ResolveSingleLatencyCount("SPOT");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh, ServiceName);
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var spotPub = pubNode.CreateSpot();
        using var spotSub = subNode.CreateSpot();

        try
        {
            int sndHwm = PerfEnv.ReadPositive("PERF_SINGLE_SNDHWM",
                PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
            int rcvHwm = PerfEnv.ReadPositive("PERF_SINGLE_RCVHWM",
                PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
            int sndTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
            int rcvTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);

            pubNode.PublisherOptions.SendHighWaterMark = sndHwm;
            pubNode.PublisherOptions.SendTimeout =
                TimeSpan.FromMilliseconds(sndTimeo);
            pubNode.PublisherOptions.NoDrop = true;
            subNode.SubscriberOptions.ReceiveHighWaterMark = rcvHwm;
            subNode.SubscriberOptions.ReceiveTimeout =
                TimeSpan.FromMilliseconds(rcvTimeo);

            ConfigureSpotTlsPublisherIfNeeded(pubNode, transport);
            ConfigureSpotTlsSubscriberIfNeeded(subNode, transport);

            string registryPub = EndpointFor(transport, "spot-registry-pub");
            string registryRouter = EndpointFor(transport,
                "spot-registry-router");
            registry.Bind(registryPub, registryRouter);
            discovery.ConnectRegistry(registryRouter);
            pubNode.AttachDiscovery(discovery);
            subNode.AttachDiscovery(discovery);

            string publisherEndpoint = EndpointFor(transport, "spot-publisher");
            string subscriberEndpoint = EndpointFor(transport, "spot-subscriber");
            pubNode.Bind(publisherEndpoint);
            subNode.Bind(subscriberEndpoint);
            spotSub.SetSubscription(Topic);

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var probePayload = new byte[payloadSize];
            var activePayload = new byte[payloadSize];
            Array.Fill(probePayload, (byte)'p');
            Array.Fill(activePayload, (byte)'a');

            long received = 0;
            long sampleSeen = 0;
            uint rng = 0xA341316Cu;
            var latencySamples = new List<double>(Math.Max(0, latencySampleCap));

            if (!WaitForSubscriptionReady(spotPub, spotSub, probePayload, size))
            {
                Console.Error.WriteLine("single_spot_error:subscription_not_ready");
                return 2;
            }

            Thread.Sleep(Math.Max(1, readySettleMs));

            long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
            ulong seq = 1;
            while (Stopwatch.GetTimestamp() < deadlineTicks)
            {
                StampMetricHeader(activePayload.AsSpan(), RunId, ActivePhase,
                    size, seq, EpochNs());
                seq++;
                try
                {
                    PerfSocketIo.Publish(spotPub, ServiceName, Topic,
                        activePayload, SendFlags.None);
                }
                catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                || IsWouldBlock(ex.InternalErrno))
                {
                    continue;
                }

                DrainSubscriber(spotSub, size, deadlineTicks, ref received,
                    latencySamples, ref sampleSeen, latencySampleCap, ref rng, true);
            }

            StampMetricHeader(activePayload.AsSpan(), RunId, CooldownPhase,
                size, seq, EpochNs());
            PerfSocketIo.Publish(spotPub, ServiceName, Topic, activePayload,
                SendFlags.None);

            long idleDeadline = DeadlineTicksFromMilliseconds(Math.Max(1,
                recvTimeoutMs));
            while (Stopwatch.GetTimestamp() < idleDeadline)
            {
                if (!DrainSubscriber(spotSub, size, long.MaxValue, ref received,
                        latencySamples, ref sampleSeen, latencySampleCap,
                        ref rng, false))
                {
                    Thread.Sleep(1);
                }
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

    private static bool WaitForSubscriptionReady(Spot publisher, Spot subscriber,
        byte[] probePayload, int msgSize)
    {
        ulong seq = 1;
        long deadlineTicks = DeadlineTicksFromMilliseconds(
            ResolveSpotReadyTimeoutMs());
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(probePayload.AsSpan(), RunId, ReadyPhase, msgSize,
                seq, EpochNs());
            seq++;
            try
            {
                PerfSocketIo.Publish(publisher, ServiceName, Topic,
                    probePayload, SendFlags.None);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
            }

            if (ProbeSubscriber(subscriber, msgSize))
            {
                return true;
            }

            Thread.Sleep(10);
        }

        return false;
    }

    private static bool DrainSubscriber(Spot subscriber, int msgSize,
        long activeDeadlineTicks, ref long received, List<double> latencySamples,
        ref long sampleSeen, int latencySampleCap, ref uint rng,
        bool countActive)
    {
        bool processed = false;
        while (true)
        {
            TopicMessage? subscribed = null;
            try
            {
                subscribed = subscriber.Subscribe(RecvFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
                return processed;
            }

            if (subscribed == null)
                return processed;

            using (subscribed)
            {
                Message body = subscribed.FirstPart();
                ReadOnlySpan<byte> payload = body.AsReadOnlySpan();
                if (TryDecodeExpectedSingleHeader(payload, msgSize, ReadyPhase,
                        out _, RunId))
                {
                    return true;
                }

                if (!countActive
                    || Stopwatch.GetTimestamp() > activeDeadlineTicks
                    || !TryDecodeExpectedSingleHeader(payload, msgSize,
                        ActivePhase, out var header, RunId))
                {
                    processed = true;
                    continue;
                }

                received++;
                ulong nowNs = EpochNs();
                if (nowNs >= header.SentTsNs)
                {
                    double latencyNs = nowNs - header.SentTsNs;
                    ReservoirSample(latencySamples, latencyNs,
                        ref sampleSeen, latencySampleCap, ref rng);
                }
                processed = true;
            }
        }
    }

    private static bool ProbeSubscriber(Spot subscriber, int msgSize)
    {
        while (true)
        {
            TopicMessage? subscribed = null;
            try
            {
                subscribed = subscriber.Subscribe(RecvFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
                return false;
            }

            if (subscribed == null)
                return false;

            using (subscribed)
            {
                Message body = subscribed.FirstPart();
                if (TryDecodeExpectedSingleHeader(body.AsReadOnlySpan(), msgSize,
                        ReadyPhase, out _, RunId))
                {
                    return true;
                }
            }
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
