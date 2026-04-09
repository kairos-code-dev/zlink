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
    internal static int Run(PerfOptions options)
    {
        SpotClientConfig config = BuildConfig(options);
        _ = ResolveMultiRecvMode(options, Pattern);

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx, options);
        var slots = new List<SpotClientSlot>(config.ClientCount);
        try
        {
            if (!TryParseSpotReadyEndpoint(options.Endpoint,
                    out string serverEndpoint))
            {
                Console.Error.WriteLine("multi_client_error:invalid_spot_ready_payload");
                return 1;
            }

            for (int i = 0; i < config.ClientCount; i++)
            {
                SpotClientSlot slot = CreateSlot(ctx, config, serverEndpoint,
                    options);
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
            result.LatencyNs, result.LatencyP95Ns, result.LatencyP99Ns);
        return 0;
    }

    private static SpotClientSlot CreateSlot(Context ctx,
        SpotClientConfig config, string serverEndpoint, PerfOptions options)
    {
        var node = new SpotNode(ctx);
        try
        {
            ConfigureSpotTlsSubscriberIfNeeded(node, config.Transport);
            ApplySpotNodeSubscriberOptions(node, options);
            var subscriber = new Spot(node);
            try
            {
                node.ConnectPeer(serverEndpoint);
                subscriber.SetSubscription("bench");
                return new SpotClientSlot(node, subscriber);
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
        for (int i = 0; i < slots.Count; i++)
        {
            if (!WaitSpotPeerConnected(slots[i].Node, timeoutMs))
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
                        ulong nowNs = EpochNs();
                        if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
                        {
                            double sampleLatencyNs = nowNs - header.SentTsNs;
                            ReservoirSample(latencySamples, sampleLatencyNs,
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
            if (!subscriber.TrySubscribe(out Subscribed? subscribed)
                || subscribed == null)
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

    private static SpotClientConfig BuildConfig(PerfOptions options)
    {
        return new SpotClientConfig(
            options.Transport,
            Math.Max(1, options.Size),
            ResolveMultiDurationSeconds(options),
            ResolveMultiLatencySampleCap(options),
            ResolveMultiClients(options),
            ResolveMultiConnectReadyTimeoutMs(options));
    }

    private static SpotClientResult ComputeResult(SpotClientActiveStats stats,
        List<double> latencySamples, int durationSeconds)
    {
        double measuredSeconds = (stats.BenchEndTicks - stats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double activeSeconds = Math.Max(1.0, durationSeconds);
        double throughput = stats.MeasureCount / activeSeconds;
        double fallbackLatencyNs = (measuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, stats.MeasureCount);
        var latency = ComputeLatencyStats(latencySamples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;
        return new SpotClientResult(throughput, latencyNs, latencyP95Ns,
            latencyP99Ns);
    }

    private static void ApplySpotNodeSubscriberOptions(SpotNode node,
        PerfOptions options)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", options);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);
        int xpubNoDrop = options.SpotXpubNoDrop > 0 ? 1 : 0;
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
        internal SpotClientResult(double throughput, double latencyNs,
            double latencyP95Ns, double latencyP99Ns)
        {
            Throughput = throughput;
            LatencyNs = latencyNs;
            LatencyP95Ns = latencyP95Ns;
            LatencyP99Ns = latencyP99Ns;
        }

        internal double Throughput { get; }
        internal double LatencyNs { get; }
        internal double LatencyP95Ns { get; }
        internal double LatencyP99Ns { get; }
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

    private sealed class SpotClientSlot : IDisposable
    {
        internal SpotClientSlot(SpotNode node, Spot subscriber)
        {
            Node = node;
            Subscriber = subscriber;
        }

        internal SpotNode Node { get; }
        internal Spot Subscriber { get; }

        public void Dispose()
        {
            Subscriber.Dispose();
            Node.Dispose();
        }
    }
}
