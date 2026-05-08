using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfSpot
{
    private const string ChannelName = "bench-svc";
    private const string Topic = "bench";
    private const uint RunId = 1;
    private const uint ReadyPhase = 0;
    private const uint ActivePhase = 1;
    private const uint CooldownPhase = 2;
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

        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int readySettleMs = ResolveSingleSpotReadySettleMs();
        int latencySampleCap = ResolveSingleLatencyCount("SPOT");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var registry = new Registry(ctx);
        using var pubDiscovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            ChannelName);
        using var subDiscovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            ChannelName);
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var spotPub = pubNode.CreateSpot();
        using var spotSub = subNode.CreateSpot();

        try
        {
            if (PerfEnv.ReadPositive("PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES",
                    PerfEnv.ReadPositive("PERF_ALLOW_MANUAL_SOCKET_OVERRIDES", 0)) > 0)
            {
                int sndHwm = PerfEnv.ReadPositive("PERF_SINGLE_SNDHWM",
                    PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
                int rcvHwm = PerfEnv.ReadPositive("PERF_SINGLE_RCVHWM",
                    PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
                pubNode.PubSubHighWaterMark = sndHwm;
                subNode.PubSubHighWaterMark = rcvHwm;
            }
            pubNode.SetRoutingId(PubNodeRoutingId);
            subNode.SetRoutingId(SubNodeRoutingId);
            spotPub.SetRoutingId(PubSpotRoutingId);
            spotSub.SetRoutingId(SubSpotRoutingId);

            ConfigureSpotNodeTlsIfNeeded(pubNode, transport);
            ConfigureSpotNodeTlsIfNeeded(subNode, transport);

            string registryPub = EndpointFor(transport, "spot-registry-pub");
            string registryRouter = EndpointFor(transport,
                "spot-registry-router");
            ConfigureSpotRegistryTlsIfNeeded(registry, transport);
            registry.Bind(registryPub, registryRouter);
            registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
            ConfigureSpotDiscoveryTlsIfNeeded(pubDiscovery, transport);
            ConfigureSpotDiscoveryTlsIfNeeded(subDiscovery, transport);
            pubDiscovery.ConnectRegistry(registryRouter);
            subDiscovery.ConnectRegistry(registryRouter);

            string publisherEndpoint = EndpointFor(transport, "spot-publisher");
            string subscriberEndpoint = EndpointFor(transport, "spot-subscriber");
            pubNode.Bind(publisherEndpoint);
            subNode.Bind(subscriberEndpoint);
            pubNode.AttachDiscovery(pubDiscovery);
            subNode.AttachDiscovery(subDiscovery);
            spotSub.SetSubscription(Topic);

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var probePayload = new byte[payloadSize];
            var activePayload = new byte[payloadSize];
            Array.Fill(probePayload, (byte)'p');
            Array.Fill(activePayload, (byte)'a');

            var dispatchState = new SpotDispatchState(size, latencySampleCap);
            spotSub.OnDispatchEvent(info =>
                OnSpotDispatchEvent(spotSub, info, dispatchState));

            if (!WaitForSubscriptionReady(spotPub, dispatchState, probePayload,
                    size))
            {
                Console.Error.WriteLine("single_spot_error:subscription_not_ready");
                return 2;
            }

            Thread.Sleep(Math.Max(1, readySettleMs));

            long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
            dispatchState.BeginActive(deadlineTicks);
            ulong seq = 1;
            while (Stopwatch.GetTimestamp() < deadlineTicks)
            {
                StampMetricHeader(activePayload.AsSpan(), RunId, ActivePhase,
                    size, seq, EpochNs());
                seq++;
                try
                {
                    PerfSocketIo.Publish(spotPub, Topic,
                        activePayload, SendFlags.None);
                }
                catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                || IsWouldBlock(ex.InternalErrno))
                {
                    continue;
                }
            }

            StampMetricHeader(activePayload.AsSpan(), RunId, CooldownPhase,
                size, seq, EpochNs());
            PerfSocketIo.Publish(spotPub, Topic, activePayload,
                SendFlags.None);

            if (!dispatchState.WaitForIdleDrain(recvTimeoutMs))
                return 2;

            long received = dispatchState.Received;
            List<double> latencySamples = dispatchState.LatencySnapshot();
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
        SpotDispatchState state, byte[] probePayload, int msgSize)
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
                PerfSocketIo.Publish(publisher, Topic,
                    probePayload, SendFlags.None);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
            }

            if (state.WaitForReady(TimeSpan.FromMilliseconds(50)))
                return true;
        }

        return state.ReadySeen;
    }

    private static void OnSpotDispatchEvent(Spot spot, SpotDispatchInfo info,
        SpotDispatchState state)
    {
        if (info.Event != SpotDispatchEvent.SubscribeReadable)
            return;

        while (true)
        {
            TopicMessage? subscribed;
            try
            {
                subscribed = spot.Subscribe(RecvFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
                return;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[single-spot] receive failed: {ex.Message}");
                state.MarkFatal();
                return;
            }

            if (subscribed == null)
                return;

            using (subscribed)
            {
                state.Record(subscribed);
            }
        }
    }

    private sealed class SpotDispatchState
    {
        private readonly object _gate = new();
        private readonly int _msgSize;
        private readonly int _latencySampleCap;
        private readonly List<double> _latencySamples;
        private long _sampleSeen;
        private uint _rng = 0xA341316Cu;
        private long _activeDeadlineTicks;
        private long _lastRecvTicks;
        private bool _collectActive;
        private bool _fatal;
        private bool _readySeen;
        private long _received;

        public SpotDispatchState(int msgSize, int latencySampleCap)
        {
            _msgSize = msgSize;
            _latencySampleCap = latencySampleCap;
            _latencySamples = new List<double>(Math.Max(0, latencySampleCap));
            _lastRecvTicks = Stopwatch.GetTimestamp();
        }

        public bool ReadySeen
        {
            get
            {
                lock (_gate)
                    return _readySeen && !_fatal;
            }
        }

        public long Received
        {
            get
            {
                lock (_gate)
                    return _received;
            }
        }

        public void BeginActive(long activeDeadlineTicks)
        {
            lock (_gate)
            {
                _collectActive = true;
                _activeDeadlineTicks = activeDeadlineTicks;
                _received = 0;
                _latencySamples.Clear();
                _sampleSeen = 0;
                _lastRecvTicks = Stopwatch.GetTimestamp();
            }
        }

        public void MarkFatal()
        {
            lock (_gate)
            {
                _fatal = true;
                Monitor.PulseAll(_gate);
            }
        }

        public void Record(TopicMessage subscribed)
        {
            Message body = subscribed.FirstPart();
            ReadOnlySpan<byte> payload = body.AsReadOnlySpan();
            long recvTicks = Stopwatch.GetTimestamp();
            lock (_gate)
            {
                _lastRecvTicks = recvTicks;
                if (TryDecodeExpectedSingleHeader(payload, _msgSize, ReadyPhase,
                        out _, RunId))
                {
                    _readySeen = true;
                    Monitor.PulseAll(_gate);
                    return;
                }

                if (!_collectActive || recvTicks > _activeDeadlineTicks
                    || !TryDecodeExpectedSingleHeader(payload, _msgSize,
                        ActivePhase, out var header, RunId))
                {
                    Monitor.PulseAll(_gate);
                    return;
                }

                _received++;
                ulong nowNs = EpochNs();
                if (nowNs >= header.SentTsNs)
                {
                    ReservoirSample(_latencySamples, nowNs - header.SentTsNs,
                        ref _sampleSeen, _latencySampleCap, ref _rng);
                }
                Monitor.PulseAll(_gate);
            }
        }

        public bool WaitForReady(TimeSpan timeout)
        {
            lock (_gate)
            {
                if (_readySeen || _fatal)
                    return _readySeen && !_fatal;
                Monitor.Wait(_gate, timeout);
                return _readySeen && !_fatal;
            }
        }

        public bool WaitForIdleDrain(int timeoutMs)
        {
            long idleTicks = Math.Max(1,
                (long)Math.Ceiling(Math.Max(1, timeoutMs)
                    * Stopwatch.Frequency / 1000.0));
            lock (_gate)
            {
                while (!_fatal)
                {
                    long elapsed = Stopwatch.GetTimestamp() - _lastRecvTicks;
                    if (elapsed >= idleTicks)
                        return true;
                    int waitMs = Math.Max(1,
                        (int)Math.Ceiling((idleTicks - elapsed) * 1000.0
                            / Stopwatch.Frequency));
                    Monitor.Wait(_gate, waitMs);
                }

                return false;
            }
        }

        public List<double> LatencySnapshot()
        {
            lock (_gate)
                return new List<double>(_latencySamples);
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
