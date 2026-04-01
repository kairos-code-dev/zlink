using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpotClient
{
    private const string Pattern = "SPOT";
    private const uint ExpectedRunId = 1;
    private const uint SpotMonitorEventError = 1u << 4;
    private const uint SpotMonitorEventSubDeliveryReadyChanged = 1u << 19;
    private const uint SpotReadyMonitorEvents = SpotMonitorEventError
        | SpotMonitorEventSubDeliveryReadyChanged;

    internal static int Run(string transport, int size, string endpoint)
    {
        SpotClientConfig config = BuildConfig(transport, size);
        PerfRecvMode recvMode = ResolveMultiRecvMode(Pattern);

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var slots = new List<SpotClientSlot>(config.ClientCount);
        try
        {
            if (!TryParseSpotReadyEndpoint(endpoint, out string serverEndpoint))
            {
                Console.Error.WriteLine("multi_client_error:invalid_spot_ready_payload");
                return 1;
            }

            for (int i = 0; i < config.ClientCount; i++)
            {
                SpotClientSlot slot = CreateSlot(ctx, config, recvMode,
                    serverEndpoint);
                slots.Add(slot);
            }

            if (slots.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            if (!WaitForSlotsReady(slots, config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            if (recvMode == PerfRecvMode.Callback)
                return RunCallbackMode(slots, config);
            return RunRecvMode(slots, config);
        }
        finally
        {
            DisposeSlots(slots);
        }
    }

    private static int RunRecvMode(List<SpotClientSlot> slots,
        SpotClientConfig config)
    {
        var recv = new byte[Math.Max(256, config.Size)];
        var state = new SpotClientPhaseState(config.LatencySampleCap);
        Console.WriteLine($"CLIENT_READY,{config.Size}");
        if (!WaitForMsgSizeStart(slots, recv, config.Size,
                config.ConnectReadyTimeoutMs))
        {
            Console.Error.WriteLine("multi_client_error:no_msg_size_start");
            return 2;
        }

        SpotClientActiveStats activeStats = RunActivePhase(slots, recv, config,
            ref state);
        SpotClientResult result = ComputeResult(activeStats, state.LatencySamples,
            config.DurationSeconds);
        PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
            result.LatencyUs, result.LatencyP95Us, result.LatencyP99Us);
        return 0;
    }

    private static int RunCallbackMode(List<SpotClientSlot> slots,
        SpotClientConfig config)
    {
        Console.WriteLine($"CLIENT_READY,{config.Size}");
        long startDeadline = DeadlineTicksFromMilliseconds(
            config.ConnectReadyTimeoutMs);
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < startDeadline)
        {
            bool sawStart = false;
            bool failed = false;
            for (int i = 0; i < slots.Count; i++)
            {
                if (slots[i].CallbackState!.Fatal)
                    failed = true;
                if (slots[i].CallbackState!.SawAnyPhase)
                    sawStart = true;
            }

            if (failed)
            {
                Console.Error.WriteLine("multi_client_error:spot_callback_failed");
                return 2;
            }
            if (sawStart)
                break;
            spin.SpinOnce();
        }

        bool ready = false;
        for (int i = 0; i < slots.Count; i++)
            ready |= slots[i].CallbackState!.SawAnyPhase;
        if (!ready)
        {
            Console.Error.WriteLine("multi_client_error:no_msg_size_start");
            return 2;
        }

        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            bool failed = false;
            for (int i = 0; i < slots.Count; i++)
            {
                if (slots[i].CallbackState!.Fatal)
                {
                    failed = true;
                    break;
                }
            }

            if (failed)
            {
                Console.Error.WriteLine("multi_client_error:spot_callback_failed");
                return 2;
            }

            spin.SpinOnce();
        }

        var samples = new List<double>(Math.Max(0, config.LatencySampleCap));
        long measureCount = 0;
        for (int i = 0; i < slots.Count; i++)
        {
            SpotCallbackState state = slots[i].CallbackState!;
            measureCount += state.MeasureCount;
            state.AppendSamples(samples);
        }

        var activeStats = new SpotClientActiveStats(measureCount,
            benchStartTicks, Stopwatch.GetTimestamp());
        SpotClientResult result = ComputeResult(activeStats, samples,
            config.DurationSeconds);
        PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
            result.LatencyUs, result.LatencyP95Us, result.LatencyP99Us);
        return 0;
    }

    private static SpotClientSlot CreateSlot(Context ctx, SpotClientConfig config,
        PerfRecvMode recvMode, string serverEndpoint)
    {
        var node = new SpotNode(ctx);
        try
        {
            ConfigureSpotTlsSubscriberIfNeeded(node, config.Transport);
            ApplySpotNodeSubscriberOptions(node);
            var subscriber = new Spot(node);
            try
            {
                ServiceMonitor monitor = subscriber.OpenMonitor(
                    SpotReadyMonitorEvents);
                try
                {
                    node.ConnectPeer(serverEndpoint);
                    subscriber.SetSubscription("bench");
                    if (recvMode == PerfRecvMode.Callback)
                    {
                        var callbackState = new SpotCallbackState(
                            config.Size, config.LatencySampleCap);
                        subscriber.SubscribeHandler((_, parts) =>
                        {
                            callbackState.OnMessage(parts);
                        });
                        return new SpotClientSlot(node, subscriber, monitor,
                            callbackState);
                    }

                    return new SpotClientSlot(node, subscriber, monitor, null);
                }
                catch
                {
                    monitor.Dispose();
                    throw;
                }
            }
            catch
            {
                subscriber.Dispose();
                throw;
            }
        }
        catch
        {
            node.Dispose();
            throw;
        }
    }

    private static bool WaitForSlotsReady(List<SpotClientSlot> slots, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        var spin = new SpinWait();
        for (int i = 0; i < slots.Count; i++)
        {
            ServiceMonitor? monitor = slots[i].Monitor;
            if (monitor == null)
                return false;

            while (Stopwatch.GetTimestamp() < deadlineTicks)
            {
                ServiceMonitorEvent? evt = monitor.TryReceive();
                if (evt == null)
                {
                    spin.SpinOnce();
                    continue;
                }
                spin.Reset();
                if (evt.Value.EventType == SpotMonitorEventError)
                    return false;
                if (evt.Value.EventType == SpotMonitorEventSubDeliveryReadyChanged
                    && evt.Value.Value > 0)
                {
                    monitor.Dispose();
                    slots[i].Monitor = null;
                    break;
                }
            }

            if (slots[i].Monitor != null)
                return false;
        }

        return true;
    }

    private static bool WaitForMsgSizeStart(List<SpotClientSlot> slots, byte[] recv,
        int msgSize, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Count; i++)
            {
                while (true)
                {
                    int n = ReceiveSpotPayload(slots[i].Subscriber, recv.AsSpan());
                    if (n <= 0)
                        break;
                    progressed = true;
                    if (!TryDecodeMetricHeader(recv.AsSpan(0, n),
                            out PerfMetricHeader header))
                    {
                        continue;
                    }
                    if (header.RunId == ExpectedRunId
                        && header.MsgSize == (uint)msgSize)
                    {
                        return true;
                    }
                }
            }

            if (!progressed)
                spin.SpinOnce();
            else
                spin.Reset();
        }

        return false;
    }

    private static SpotClientActiveStats RunActivePhase(
        List<SpotClientSlot> slots, byte[] recv, SpotClientConfig config,
        ref SpotClientPhaseState state)
    {
        List<double> latencySamples = state.LatencySamples;
        long sampleSeen = state.SampleSeen;
        uint rng = state.Rng;
        long measureCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchEndTicks = benchStartTicks;
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        var spin = new SpinWait();

        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            bool sawData = false;
            for (int i = 0; i < slots.Count; i++)
            {
                while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
                {
                    int n = ReceiveSpotPayload(slots[i].Subscriber, recv.AsSpan());
                    if (n <= 0)
                        break;

                    sawData = true;
                    long endTicks = Stopwatch.GetTimestamp();
                    if (TryDecodeMetricHeader(recv.AsSpan(0, n),
                            out PerfMetricHeader header)
                        && header.RunId == ExpectedRunId
                        && header.MsgSize == (uint)config.Size
                        && header.Phase == (uint)PerfPhase.Active)
                    {
                        measureCount++;
                        benchEndTicks = endTicks;
                        ulong nowUs = EpochUs();
                        if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                        {
                            double sampleLatencyUs = nowUs - header.SentTsUs;
                            ReservoirSample(latencySamples, sampleLatencyUs,
                                ref sampleSeen, config.LatencySampleCap,
                                ref rng);
                        }
                    }
                }

                if (Stopwatch.GetTimestamp() >= benchDeadlineTicks)
                    break;
            }

            if (!sawData)
                spin.SpinOnce();
            else
                spin.Reset();
        }

        if (benchEndTicks <= benchStartTicks)
            benchEndTicks = Stopwatch.GetTimestamp();
        state.SampleSeen = sampleSeen;
        state.Rng = rng;

        return new SpotClientActiveStats(measureCount, benchStartTicks,
            benchEndTicks);
    }

    private static int ReceiveSpotPayload(Spot subscriber, Span<byte> payloadBuffer)
    {
        try
        {
            Subscribed? subscribed = subscriber.TrySubscribe();
            if (subscribed == null)
                return 0;
            return CopySubscribedPayload(subscribed, payloadBuffer);
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
        {
            return 0;
        }
    }

    private static int CopySubscribedPayload(Subscribed subscribed,
        Span<byte> payloadBuffer)
    {
        try
        {
            if (!subscribed.HasSinglePart)
                return 0;
            return subscribed.SinglePartOrThrow().CopyTo(payloadBuffer);
        }
        finally
        {
            foreach (Message part in subscribed.Parts)
                part.Dispose();
        }
    }

    private static SpotClientConfig BuildConfig(string transport, int size)
    {
        return new SpotClientConfig(
            transport,
            Math.Max(1, size),
            ResolveMultiDurationSeconds(),
            ResolveMultiLatencySampleCap(),
            ResolveMultiClients(Pattern),
            ResolveMultiConnectReadyTimeoutMs());
    }

    private static SpotClientResult ComputeResult(SpotClientActiveStats stats,
        List<double> latencySamples, int durationSeconds)
    {
        double measuredSeconds = (stats.BenchEndTicks - stats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double activeSeconds = Math.Max(1.0, durationSeconds);
        double throughput = stats.MeasureCount / activeSeconds;
        double fallbackLatencyUs = (measuredSeconds * 1_000_000.0)
            / Math.Max(1.0, stats.MeasureCount);
        var latency = ComputeLatencyStats(latencySamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
        return new SpotClientResult(throughput, latencyUs, latencyP95Us,
            latencyP99Us);
    }

    private static void ApplySpotNodeSubscriberOptions(SpotNode node)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", Pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", Pattern);
        int rcvTimeo = ResolveMultiRcvTimeoutMs();
        int xpubNoDrop = ParsePositiveEnv("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0
            ? 1 : 0;
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvHwm, rcvHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvTimeo, rcvTimeo);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.XPubNoDrop, xpubNoDrop);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndHwm, sndHwm);
    }

    private static void TrySetSpotNodeSocketOption(SpotNode node,
        SpotNodeSocketRole role, SocketOptionKey<int> option, int value)
    {
        try
        {
            node.SetOption(role, option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.Errno))
        {
        }
    }

    private static bool ShouldIgnoreSpotOptionError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.ENotSup
               || code == ErrorCode.EInval
               || code == ErrorCode.EProtoNoSupport;
    }

    private static bool TryParseSpotReadyEndpoint(string endpoint,
        out string serverEndpoint)
    {
        serverEndpoint = string.Empty;
        if (string.IsNullOrWhiteSpace(endpoint))
            return false;

        string[] parts = endpoint.Split('|', StringSplitOptions.TrimEntries);
        serverEndpoint = parts.Length == 0 ? string.Empty : parts[0];
        return !string.IsNullOrWhiteSpace(serverEndpoint);
    }

    private static void DisposeSlots(List<SpotClientSlot> slots)
    {
        for (int i = slots.Count - 1; i >= 0; i--)
            slots[i].Dispose();
    }

    private readonly struct SpotClientConfig
    {
        internal SpotClientConfig(string transport, int size,
            int durationSeconds, int latencySampleCap, int clientCount,
            int connectReadyTimeoutMs)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            LatencySampleCap = latencySampleCap;
            ClientCount = clientCount;
            ConnectReadyTimeoutMs = connectReadyTimeoutMs;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int LatencySampleCap { get; }
        internal int ClientCount { get; }
        internal int ConnectReadyTimeoutMs { get; }
    }

    private readonly struct SpotClientResult
    {
        internal SpotClientResult(double throughput, double latencyUs,
            double latencyP95Us, double latencyP99Us)
        {
            Throughput = throughput;
            LatencyUs = latencyUs;
            LatencyP95Us = latencyP95Us;
            LatencyP99Us = latencyP99Us;
        }

        internal double Throughput { get; }
        internal double LatencyUs { get; }
        internal double LatencyP95Us { get; }
        internal double LatencyP99Us { get; }
    }

    private readonly struct SpotClientActiveStats
    {
        internal SpotClientActiveStats(long measureCount, long benchStartTicks,
            long benchEndTicks)
        {
            MeasureCount = measureCount;
            BenchStartTicks = benchStartTicks;
            BenchEndTicks = benchEndTicks;
        }

        internal long MeasureCount { get; }
        internal long BenchStartTicks { get; }
        internal long BenchEndTicks { get; }
    }

    private struct SpotClientPhaseState
    {
        internal SpotClientPhaseState(int latencySampleCap)
        {
            LatencySamples = new List<double>(Math.Max(0, latencySampleCap));
            SampleSeen = 0;
            Rng = 0xC0FFEEu;
        }

        internal List<double> LatencySamples;
        internal long SampleSeen;
        internal uint Rng;
    }

    private sealed class SpotCallbackState
    {
        private readonly int _expectedMsgSize;
        private readonly double[] _samples;
        private readonly object _samplesLock = new object();
        private int _sampleWriteIndex;
        private int _sawAnyPhase;
        private int _fatal;
        private long _measureCount;

        internal SpotCallbackState(int expectedMsgSize, int latencySampleCap)
        {
            _expectedMsgSize = expectedMsgSize;
            _samples = new double[Math.Max(1, latencySampleCap)];
            _sampleWriteIndex = 0;
            _sawAnyPhase = 0;
            _fatal = 0;
            _measureCount = 0;
        }

        internal bool SawAnyPhase => Volatile.Read(ref _sawAnyPhase) != 0;
        internal bool Fatal => Volatile.Read(ref _fatal) != 0;
        internal long MeasureCount => Interlocked.Read(ref _measureCount);

        internal void OnMessage(Message[] parts)
        {
            try
            {
                if (parts.Length != 1)
                    return;
                if (!TryDecodeMetricHeader(parts[0].AsReadOnlySpan(),
                        out PerfMetricHeader header))
                {
                    return;
                }

                if (header.RunId != ExpectedRunId
                    || header.MsgSize != (uint)_expectedMsgSize)
                {
                    return;
                }

                Volatile.Write(ref _sawAnyPhase, 1);
                if (header.Phase != (uint)PerfPhase.Active)
                    return;

                Interlocked.Increment(ref _measureCount);
                if (header.SentTsUs == 0)
                    return;
                ulong nowUs = EpochUs();
                if (nowUs < header.SentTsUs)
                    return;
                AddSample(nowUs - header.SentTsUs);
            }
            catch
            {
                Volatile.Write(ref _fatal, 1);
            }
            finally
            {
                for (int i = 0; i < parts.Length; i++)
                    parts[i].Dispose();
            }
        }

        internal void AddSample(double latencyUs)
        {
            lock (_samplesLock)
            {
                int index = _sampleWriteIndex;
                if ((uint)index >= (uint)_samples.Length)
                    return;
                _samples[index] = latencyUs;
                _sampleWriteIndex = index + 1;
            }
        }

        internal void AppendSamples(List<double> destination)
        {
            lock (_samplesLock)
            {
                int count = Math.Min(_sampleWriteIndex, _samples.Length);
                for (int i = 0; i < count; i++)
                    destination.Add(_samples[i]);
            }
        }
    }

    private sealed class SpotClientSlot : IDisposable
    {
        internal SpotClientSlot(SpotNode node, Spot subscriber,
            ServiceMonitor monitor, SpotCallbackState? callbackState)
        {
            Node = node;
            Subscriber = subscriber;
            Monitor = monitor;
            CallbackState = callbackState;
        }

        internal SpotNode Node { get; }
        internal Spot Subscriber { get; }
        internal ServiceMonitor? Monitor { get; set; }
        internal SpotCallbackState? CallbackState { get; }

        public void Dispose()
        {
            Monitor?.Dispose();
            Subscriber.Dispose();
            Node.Dispose();
        }
    }
}
