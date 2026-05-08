using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotClient
{
    private const string Pattern = "SPOT";
    private const uint ExpectedRunId = 1;
    private const string Topic = "bench";

    internal static int Run(PerfOptions options)
    {
        if (!TryParseSpotEndpoints(options.Endpoint, out string dataEndpoint,
                out string registryEndpoint))
        {
            Console.Error.WriteLine("multi_client_error:invalid_spot_ready_payload");
            return 1;
        }

        string serviceName = MultiSpotServiceName(registryEndpoint);
        SpotClientConfig config = BuildConfig(options,
            NormalizeClientEndpoint(dataEndpoint, options.Transport),
            NormalizeClientEndpoint(registryEndpoint, options.Transport),
            serviceName);
        using var ctx = new Context();
        using var controlState = new RunnerControlState(config.Size);
        ApplyMultiClientContextOptions(ctx, options);

        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            config.ServiceName);
        using var node = new SpotNode(ctx);

        ConfigureSpotDiscoveryTlsIfNeeded(discovery, config.Transport);
        ConfigureSpotNodeTlsIfNeeded(node, config.Transport);
        ApplySpotSubscriberOptions(node, options);
        discovery.ConnectRegistry(config.RegistryEndpoint);
        node.Bind(MultiEndpointFor(config.Transport, "multi-spot-client", options));
        node.AttachDiscovery(discovery);

        var slots = new List<SpotClientSlot>(config.ClientCount);
        try
        {
            for (int i = 0; i < config.ClientCount; i++)
            {
                var subscriber = node.CreateSpot();
                subscriber.SetSubscription(Topic);
                slots.Add(new SpotClientSlot(subscriber,
                    new SpotClientSlotState(config.LatencySampleCap)));
            }

            if (!WaitForSubscriptionReady(node, config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:subscription_not_ready");
                return 2;
            }

            if (config.ReadySettleMs > 0)
                Thread.Sleep(config.ReadySettleMs);

            WriteStdoutLine($"CLIENT_READY,{config.Size}");
            if (!controlState.WaitForStart(config.ConnectReadyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            return RunActivePhase(slots, controlState, config);
        }
        finally
        {
            DisposeSlots(slots);
        }
    }

    private static SpotClientConfig BuildConfig(PerfOptions options,
        string dataEndpoint, string registryEndpoint, string serviceName)
    {
        return new SpotClientConfig(
            options.Transport,
            Math.Max(1, options.Size),
            Math.Max(1, ResolveMultiDurationSeconds(options)),
            ResolveMultiLatencySampleCap(options),
            Math.Max(1, ResolveMultiClients(options)),
            ResolveMultiConnectReadyTimeoutMs(options),
            PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000),
            ResolveMultiSpotRouteWarmupMs(),
            dataEndpoint,
            registryEndpoint,
            serviceName);
    }

    private static int RunActivePhase(List<SpotClientSlot> slots,
        RunnerControlState controlState, SpotClientConfig config)
    {
        long activeDeadlineTicks = DeadlineTicksFromMilliseconds(
            (config.DurationSeconds * 1000) + config.RouteWarmupMs);
        long cooldownDeadlineTicks = DeadlineTicksFromMilliseconds(
            config.DurationSeconds * 1000 + config.RouteWarmupMs
            + config.ConnectReadyTimeoutMs);
        var workers = new List<Thread>(slots.Count);
        for (int i = 0; i < slots.Count; i++)
        {
            SpotClientSlot slot = slots[i];
            var worker = new Thread(() => ReceiveLoop(slot, config.Size,
                activeDeadlineTicks, cooldownDeadlineTicks))
            {
                IsBackground = true,
                Name = $"multi-spot-client-{i}",
            };
            workers.Add(worker);
            worker.Start();
        }

        foreach (Thread worker in workers)
            worker.Join();

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

    private static void ReceiveLoop(SpotClientSlot slot, int msgSize,
        long activeDeadlineTicks, long cooldownDeadlineTicks)
    {
        try
        {
            while (Stopwatch.GetTimestamp() < cooldownDeadlineTicks)
            {
                using TopicMessage? subscribed = slot.Subscriber.Subscribe(
                    RecvFlags.DontWait);
                if (subscribed == null)
                {
                    Thread.Sleep(1);
                    continue;
                }

                ReadOnlySpan<byte> payload = subscribed.SinglePartOrThrow().AsReadOnlySpan();
                if (!PerfShared.TryDecodeMetricHeader(payload,
                        out PerfMetricHeader header))
                {
                    continue;
                }
                if (header.RunId != ExpectedRunId
                    || header.MsgSize != (uint)msgSize)
                {
                    continue;
                }
                if (header.Phase == (uint)PerfPhase.Cooldown)
                {
                    slot.State.CooldownSeen = 1;
                    return;
                }
                if (header.Phase != (uint)PerfPhase.Active
                    || Stopwatch.GetTimestamp() > activeDeadlineTicks)
                {
                    continue;
                }

                slot.State.MeasureCount++;
                ulong nowNs = EpochNs();
                if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
                {
                    double sampleLatencyNs = nowNs - header.SentTsNs;
                    ReservoirSample(slot.State.LatencySamples, sampleLatencyNs,
                        ref slot.State.SampleSeen, slot.State.LatencySampleCap,
                        ref slot.State.Rng);
                }
            }
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
        }
        catch (Exception ex)
        {
            Interlocked.CompareExchange(ref slot.State.Error, ex, null);
        }
    }

    private static void MergeLatencySamples(IReadOnlyList<double> source,
        List<double> target, ref long seenCount, int cap, ref uint rng)
    {
        for (int i = 0; i < source.Count; i++)
            ReservoirSample(target, source[i], ref seenCount, cap, ref rng);
    }

    private static void ApplySpotSubscriberOptions(SpotNode node,
        PerfOptions options)
    {
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        TrySetSpotOption(() => node.PubSubHighWaterMark = rcvHwm);
    }

    private static bool WaitForSubscriptionReady(SpotNode node, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(Math.Max(1,
            timeoutMs));
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                if (node.StatusSnapshot().ReadySubjectCount > 0)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }

            Thread.Sleep(10);
        }

        return false;
    }

    private static bool ShouldIgnoreSpotOptionError(int errno)
    {
        return errno == 22 || errno == 93 || errno == 95 || errno == 97;
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

    private static bool TryParseSpotEndpoints(string endpoint,
        out string dataEndpoint, out string registryEndpoint)
    {
        dataEndpoint = string.Empty;
        registryEndpoint = string.Empty;
        if (string.IsNullOrWhiteSpace(endpoint))
            return false;

        string[] parts = endpoint.Split('|', StringSplitOptions.TrimEntries);
        if (parts.Length != 2)
            return false;

        dataEndpoint = parts[0];
        registryEndpoint = parts[1];
        return !string.IsNullOrWhiteSpace(dataEndpoint)
               && !string.IsNullOrWhiteSpace(registryEndpoint);
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

    private readonly struct SpotClientConfig
    {
        internal SpotClientConfig(string transport, int size,
            int durationSeconds, int latencySampleCap, int clientCount,
            int connectReadyTimeoutMs, int readySettleMs, int routeWarmupMs,
            string dataEndpoint, string registryEndpoint, string serviceName)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            LatencySampleCap = latencySampleCap;
            ClientCount = clientCount;
            ConnectReadyTimeoutMs = connectReadyTimeoutMs;
            ReadySettleMs = readySettleMs;
            RouteWarmupMs = routeWarmupMs;
            DataEndpoint = dataEndpoint;
            RegistryEndpoint = registryEndpoint;
            ServiceName = serviceName;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int LatencySampleCap { get; }
        internal int ClientCount { get; }
        internal int ConnectReadyTimeoutMs { get; }
        internal int ReadySettleMs { get; }
        internal int RouteWarmupMs { get; }
        internal string DataEndpoint { get; }
        internal string RegistryEndpoint { get; }
        internal string ServiceName { get; }
    }

    private sealed class SpotClientSlot : IDisposable
    {
        internal SpotClientSlot(Spot subscriber, SpotClientSlotState state)
        {
            Subscriber = subscriber;
            State = state;
        }

        internal Spot Subscriber { get; }
        internal SpotClientSlotState State { get; }

        public void Dispose()
        {
            State.Dispose();
            Subscriber.Dispose();
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
        internal int CooldownSeen;
        internal Exception? Error;

        public void Dispose()
        {
        }
    }
}
