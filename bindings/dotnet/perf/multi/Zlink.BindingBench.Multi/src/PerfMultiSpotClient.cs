using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfMultiSpotClient
{
    private const string Pattern = "SPOT";
    private const uint ExpectedRunId = 1;
    private const string Topic = "bench";

    internal static int Run(PerfOptions options)
    {
        if (!TryParseSpotEndpoints(options.Endpoint, out string dataEndpoint,
                out string controlEndpoint))
        {
            Console.Error.WriteLine("multi_client_error:invalid_spot_ready_payload");
            return 1;
        }

        SpotClientConfig config = BuildConfig(options, dataEndpoint, controlEndpoint);
        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx, options);
        var slots = new List<SpotClientSlot>(config.ClientCount);
        try
        {
            for (int i = 0; i < config.ClientCount; i++)
                slots.Add(CreateSlot(ctx, config, options, i));

            if (!SendConnectedAndReadyCounts(slots, config))
                return 2;

            WriteStdoutLine($"CLIENT_READY,{config.Size}");
            if (!WaitForStart(slots, config))
            {
                Console.Error.WriteLine("multi_client_error:no_msg_size_start");
                return 2;
            }

            return RunActivePhase(slots, config);
        }
        finally
        {
            DisposeSlots(slots);
        }
    }

    private static SpotClientConfig BuildConfig(PerfOptions options,
        string dataEndpoint, string controlEndpoint)
    {
        return new SpotClientConfig(
            options.Transport,
            Math.Max(1, options.Size),
            Math.Max(1, ResolveMultiDurationSeconds(options)),
            ResolveMultiLatencySampleCap(options),
            Math.Max(1, ResolveMultiClients(options)),
            ResolveMultiConnectReadyTimeoutMs(options),
            PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000),
            PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25),
            dataEndpoint,
            controlEndpoint);
    }

    private static SpotClientSlot CreateSlot(Context ctx,
        SpotClientConfig config, PerfOptions options, int index)
    {
        var control = new DealerSocket(ctx);
        var node = new SpotNode(ctx);
        var subscriber = node.CreateSpot();
        var state = new SpotClientSlotState(config.LatencySampleCap);

        try
        {
            ApplyMultiSocketOptions(control, options);
            ConfigureTlsClientIfNeeded(control, config.Transport);
            control.SetRoutingId(RoutingId.FromBytes(
                Encoding.ASCII.GetBytes($"spot-client-{index}")));
            control.Connect(config.ControlEndpoint);
            WriteStdoutLine($"CONTROL_CONNECTED,{config.ControlEndpoint}");

            ConfigureSpotTlsSubscriberIfNeeded(node, config.Transport);
            ApplySpotSubscriberOptions(node, options);
            node.AttachChannelDealerManual("bench-svc", control);
            node.ConnectPeer(config.DataEndpoint);
            subscriber.SetSubscription(Topic);
            subscriber.OnDispatchEvent((_, info) =>
            {
                if (info.Event != SpotDispatchEvent.SubscribeReadable)
                    return;

                DrainSubscriber(subscriber, state, config.Size);
            });

            return new SpotClientSlot(node, control, subscriber, state);
        }
        catch
        {
            state.Dispose();
            subscriber.Dispose();
            node.Dispose();
            control.Dispose();
            throw;
        }
    }

    private static bool SendConnectedAndReadyCounts(
        List<SpotClientSlot> slots, SpotClientConfig config)
    {
        for (int i = 0; i < slots.Count; i++)
        {
            SendControlLine(slots[i].Control, "CONNECTED");
        }

        if (config.ReadySettleMs > 0)
            Thread.Sleep(config.ReadySettleMs);
        if (config.ControlSettleMs > 0)
            Thread.Sleep(config.ControlSettleMs);

        for (int i = 0; i < slots.Count; i++)
        {
            SendControlLine(slots[i].Control, $"READY_COUNT,{config.Size},1");
        }

        return true;
    }

    private static bool WaitForStart(List<SpotClientSlot> slots,
        SpotClientConfig config)
    {
        using var pollManager = new PollManager();
        var sockets = new List<SocketBase>(slots.Count);
        for (int i = 0; i < slots.Count; i++)
            sockets.Add(slots[i].Control);

        long deadlineTicks = DeadlineTicksFromMilliseconds(config.ConnectReadyTimeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int timeoutMs = Math.Max(1,
                (int)Math.Ceiling((deadlineTicks - Stopwatch.GetTimestamp())
                    * 1000.0 / Stopwatch.Frequency));
            if (PollSocketReadReady(pollManager, sockets, timeoutMs) <= 0)
                continue;

            for (int i = 0; i < slots.Count; i++)
            {
                if (!IsSocketReadReady(pollManager, i) || slots[i].Started)
                    continue;

                while (TryRecvControl(slots[i].Control, out Received? received))
                {
                    Received controlMessage = received
                        ?? throw new InvalidOperationException("Control recv returned null.");
                    using (controlMessage)
                    {
                        string line = controlMessage.SinglePartOrThrow().GetString();
                        if (TryParseStart(line, config.Size))
                            slots[i].Started = true;
                    }
                }
            }

            bool allStarted = true;
            for (int i = 0; i < slots.Count; i++)
            {
                if (!slots[i].Started)
                {
                    allStarted = false;
                    break;
                }
            }

            if (allStarted)
                return true;
        }

        return false;
    }

    private static int RunActivePhase(List<SpotClientSlot> slots,
        SpotClientConfig config)
    {
        long deadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);
        for (int i = 0; i < slots.Count; i++)
        {
            slots[i].State.ActiveDeadlineTicks = deadlineTicks;
            slots[i].State.ActiveOpen = 1;
        }

        int waitMs = Math.Max(1, config.DurationSeconds * 1000);
        using (var activeWait = new ManualResetEventSlim(false))
            activeWait.Wait(waitMs);

        for (int i = 0; i < slots.Count; i++)
            slots[i].State.ActiveOpen = 0;

        long measureCount = 0;
        var samples = new List<double>(Math.Max(0, config.LatencySampleCap));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        for (int i = 0; i < slots.Count; i++)
        {
            if (slots[i].State.Error != null)
            {
                Console.Error.WriteLine($"multi_client_error:spot_recv:{slots[i].State.Error}");
                return 2;
            }

            measureCount += slots[i].State.MeasureCount;
            MergeLatencySamples(slots[i].State.LatencySamples, samples,
                ref sampleSeen, config.LatencySampleCap, ref rng);
        }

        if (measureCount <= 0)
            return 2;

        double activeSeconds = Math.Max(1.0, config.DurationSeconds);
        double throughput = measureCount / activeSeconds;
        double fallbackLatencyNs = (activeSeconds * 1_000_000_000.0)
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(samples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;

        PrintResult(Pattern, config.Transport, config.Size, throughput,
            latencyNs, latencyP95Ns, latencyP99Ns);
        return 0;
    }

    private static void MergeLatencySamples(IReadOnlyList<double> source,
        List<double> target, ref long seenCount, int cap, ref uint rng)
    {
        for (int i = 0; i < source.Count; i++)
        {
            ReservoirSample(target, source[i], ref seenCount, cap, ref rng);
        }
    }

    private static void DrainSubscriber(Spot subscriber,
        SpotClientSlotState state, int msgSize)
    {
        try
        {
            while (true)
            {
                using TopicMessage? subscribed = subscriber.Subscribe(
                    RecvFlags.DontWait);
                if (subscribed == null)
                    break;

                ReadOnlySpan<byte> payload = subscribed.SinglePartOrThrow().AsReadOnlySpan();
                long recvTicks = Stopwatch.GetTimestamp();
                if (state.ActiveOpen == 0 || recvTicks > state.ActiveDeadlineTicks)
                    continue;

                if (!PerfShared.TryDecodeMetricHeader(payload,
                        out PerfMetricHeader header))
                    continue;
                if (header.RunId != ExpectedRunId
                    || header.MsgSize != (uint)msgSize
                    || header.Phase != (uint)PerfPhase.Active)
                {
                    continue;
                }

                state.MeasureCount++;
                ulong nowNs = EpochNs();
                if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
                {
                    double sampleLatencyNs = nowNs - header.SentTsNs;
                    ReservoirSample(state.LatencySamples, sampleLatencyNs,
                        ref state.SampleSeen, state.LatencySampleCap,
                        ref state.Rng);
                }
            }
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
        }
        catch (Exception ex)
        {
            Interlocked.CompareExchange(ref state.Error, ex, null);
        }
    }

    private static void ApplySpotSubscriberOptions(SpotNode node,
        PerfOptions options)
    {
        int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);
        bool xpubNoDrop = options.SpotXpubNoDrop > 0;

        TrySetSpotOption(() => node.SubscriberOptions.Linger = TimeSpan.Zero);
        TrySetSpotOption(() => node.SubscriberOptions.ReceiveHighWaterMark = rcvHwm);
        TrySetSpotOption(() => node.SubscriberOptions.ReceiveTimeout =
            TimeSpan.FromMilliseconds(rcvTimeo));
        TrySetSpotOption(() => node.PublisherOptions.Linger = TimeSpan.Zero);
        TrySetSpotOption(() => node.PublisherOptions.SendHighWaterMark = sndHwm);
        TrySetSpotOption(() => node.PublisherOptions.NoDrop = xpubNoDrop);
    }

    private static bool ShouldIgnoreSpotOptionError(int errno)
    {
        return errno == 22 || errno == 93 || errno == 95 || errno == 97;
    }

    private static void SendControlLine(DealerSocket control, string line)
    {
        byte[] payload = Encoding.ASCII.GetBytes(line);
        PerfSocketIo.Send(control, payload, SendFlags.None);
    }

    private static bool TryRecvControl(DealerSocket control,
        out Received? received)
    {
        try
        {
            received = control.Recv(RecvFlags.DontWait);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            received = null;
            return false;
        }
    }

    private static bool TryParseSpotEndpoints(string endpoint,
        out string dataEndpoint, out string controlEndpoint)
    {
        dataEndpoint = string.Empty;
        controlEndpoint = string.Empty;
        if (string.IsNullOrWhiteSpace(endpoint))
            return false;

        string[] parts = endpoint.Split('|', StringSplitOptions.TrimEntries);
        if (parts.Length != 2)
            return false;

        dataEndpoint = parts[0];
        controlEndpoint = parts[1];
        return !string.IsNullOrWhiteSpace(dataEndpoint)
               && !string.IsNullOrWhiteSpace(controlEndpoint);
    }

    private static bool TryParseStart(string line, int msgSize)
    {
        const string prefix = "START,";
        if (!line.StartsWith(prefix, StringComparison.Ordinal))
            return false;
        return int.TryParse(line.AsSpan(prefix.Length), out int parsed)
               && parsed == msgSize;
    }

    private static void TrySetSpotOption(Action configure)
    {
        try
        {
            configure();
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.InternalErrno))
        {
        }
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
            int connectReadyTimeoutMs, int readySettleMs, int controlSettleMs,
            string dataEndpoint, string controlEndpoint)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            LatencySampleCap = latencySampleCap;
            ClientCount = clientCount;
            ConnectReadyTimeoutMs = connectReadyTimeoutMs;
            ReadySettleMs = readySettleMs;
            ControlSettleMs = controlSettleMs;
            DataEndpoint = dataEndpoint;
            ControlEndpoint = controlEndpoint;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int LatencySampleCap { get; }
        internal int ClientCount { get; }
        internal int ConnectReadyTimeoutMs { get; }
        internal int ReadySettleMs { get; }
        internal int ControlSettleMs { get; }
        internal string DataEndpoint { get; }
        internal string ControlEndpoint { get; }
    }

    private sealed class SpotClientSlot : IDisposable
    {
        internal SpotClientSlot(SpotNode node, DealerSocket control,
            Spot subscriber, SpotClientSlotState state)
        {
            Node = node;
            Control = control;
            Subscriber = subscriber;
            State = state;
        }

        internal SpotNode Node { get; }
        internal DealerSocket Control { get; }
        internal Spot Subscriber { get; }
        internal SpotClientSlotState State { get; }
        internal bool Started { get; set; }

        public void Dispose()
        {
            State.Dispose();
            Subscriber.Dispose();
            Node.Dispose();
            Control.Dispose();
        }
    }

    private sealed class SpotClientSlotState : IDisposable
    {
        internal SpotClientSlotState(int latencySampleCap)
        {
            LatencySampleCap = Math.Max(0, latencySampleCap);
            LatencySamples = new List<double>(LatencySampleCap);
            Rng = 0xC0FFEEu;
        }

        internal List<double> LatencySamples { get; }
        internal int LatencySampleCap { get; }
        internal long SampleSeen;
        internal uint Rng;
        internal long MeasureCount;
        internal long ActiveDeadlineTicks;
        internal int ActiveOpen;
        internal Exception? Error;

        public void Dispose()
        {
        }
    }
}
