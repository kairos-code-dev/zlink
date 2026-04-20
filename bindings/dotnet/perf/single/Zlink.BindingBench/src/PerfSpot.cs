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
        int readyTimeoutMs = ResolveSpotReadyTimeoutMs();
        int readySettleMs = ResolveSingleSpotReadySettleMs();
        int latencySampleCap = ResolveSingleLatencyCount("SPOT");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, ServiceType.Spot, ServiceName);
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var spotPub = new Spot(pubNode);
        using var spotSub = new Spot(subNode);

        try
        {
            int sndHwm = PerfEnv.ReadPositive("PERF_SINGLE_SNDHWM",
                PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
            int rcvHwm = PerfEnv.ReadPositive("PERF_SINGLE_RCVHWM",
                PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
            int sndTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
            int rcvTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);

            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndHwm,
                sndHwm);
            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndTimeo,
                sndTimeo);
            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.XPubNoDrop,
                1);
            subNode.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvHwm,
                rcvHwm);
            subNode.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvTimeo,
                rcvTimeo);

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
            long lastRecvTicks = Stopwatch.GetTimestamp();
            long activeDeadlineTicks = 0;
            uint rng = 0xA341316Cu;
            int readySeen = 0;
            int activeOpen = 0;
            Exception? recvError = null;
            var latencySamples = new List<double>(Math.Max(0, latencySampleCap));

            void DrainSubscriber()
            {
                while (true)
                {
                    TopicMessage? subscribed = null;
                    try
                    {
                        subscribed = spotSub.Subscribe(RecvFlags.DontWait);
                    }
                    catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                    || IsWouldBlock(ex.InternalErrno))
                    {
                        return;
                    }

                    using (subscribed)
                    {
                        Message body = subscribed.FirstPart();
                        ReadOnlySpan<byte> payload = body.AsReadOnlySpan();
                        long recvTicks = Stopwatch.GetTimestamp();
                        Volatile.Write(ref lastRecvTicks, recvTicks);

                        if (TryDecodeExpectedSingleHeader(payload, size,
                                ReadyPhase, out _, RunId))
                        {
                            Volatile.Write(ref readySeen, 1);
                            continue;
                        }

                        if (Volatile.Read(ref activeOpen) == 0
                            || recvTicks > Volatile.Read(ref activeDeadlineTicks))
                        {
                            continue;
                        }

                        if (!TryDecodeExpectedSingleHeader(payload, size,
                                ActivePhase, out var header, RunId))
                        {
                            continue;
                        }

                        Interlocked.Increment(ref received);
                        ulong nowNs = EpochNs();
                        if (nowNs >= header.SentTsNs)
                        {
                            double latencyNs = nowNs - header.SentTsNs;
                            ReservoirSample(latencySamples, latencyNs,
                                ref sampleSeen, latencySampleCap, ref rng);
                        }
                    }
                }
            }

            spotSub.OnDispatchEvent(evt =>
            {
                if (evt != SpotDispatchEvent.SubscribeReadable)
                    return;

                try
                {
                    DrainSubscriber();
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref recvError, ex, null);
                }
            });

            if (!WaitForSubscriptionReady(spotPub, probePayload, size,
                    readyTimeoutMs, ref readySeen))
            {
                Console.Error.WriteLine("single_spot_error:subscription_not_ready");
                return 2;
            }

            if (recvError != null)
                return 2;

            Thread.Sleep(Math.Max(1, readySettleMs));

            long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
            Volatile.Write(ref activeDeadlineTicks, deadlineTicks);
            Volatile.Write(ref activeOpen, 1);

            bool sendFailed = false;
            ulong seq = 1;
            var senderThread = new Thread(() =>
            {
                while (Stopwatch.GetTimestamp() < deadlineTicks)
                {
                    StampMetricHeader(activePayload.AsSpan(), RunId, ActivePhase,
                        size, seq, EpochNs());
                    seq++;
                    try
                    {
                        using var message = Message.FromBytes(activePayload);
                        spotPub.Publish(ServiceName, Topic, message);
                    }
                    catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                    || IsWouldBlock(ex.InternalErrno))
                    {
                        continue;
                    }
                    catch
                    {
                        sendFailed = true;
                        break;
                    }
                }
            });
            senderThread.IsBackground = true;
            senderThread.Start();
            senderThread.Join();

            Volatile.Write(ref activeOpen, 0);
            WaitForIdleDrain(ref lastRecvTicks, recvTimeoutMs, ref recvError);

            if (recvError != null || sendFailed)
                return 2;
            if (received <= 0 || latencySamples.Count == 0)
                return 2;

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("SPOT", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            ctx.Shutdown();
            return 0;
        }
        catch (Exception ex)
        {
            if (TryPrintUnsupportedTransportFailure("SPOT", transport, size, ex))
                return 0;
            Console.Error.WriteLine(
                $"single_spot_error:exception:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
    }

    private static bool WaitForSubscriptionReady(Spot publisher, byte[] probePayload,
        int msgSize, int timeoutMs, ref int readySeen)
    {
        ulong seq = 1;
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (Volatile.Read(ref readySeen) != 0)
                return true;

            StampMetricHeader(probePayload.AsSpan(), RunId, ReadyPhase, msgSize,
                seq, EpochNs());
            seq++;
            try
            {
                using var message = Message.FromBytes(probePayload);
                publisher.Publish(ServiceName, Topic, message);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
            }

            if (Volatile.Read(ref readySeen) != 0)
                return true;

            spin.SpinOnce();
        }

        return Volatile.Read(ref readySeen) != 0;
    }

    private static void WaitForIdleDrain(ref long lastRecvTicks,
        int recvTimeoutMs, ref Exception? recvError)
    {
        long recvFlushTicks = Math.Max(1,
            (long)Math.Ceiling(recvTimeoutMs * Stopwatch.Frequency / 1000.0));
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() - Volatile.Read(ref lastRecvTicks)
               < recvFlushTicks)
        {
            if (recvError != null)
                return;
            spin.SpinOnce();
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
        return ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain;
    }

    private static bool IsInterrupted(int errno)
    {
        return ZlinkException.MapErrorCode(errno) == ErrorCode.EIntr;
    }
}
