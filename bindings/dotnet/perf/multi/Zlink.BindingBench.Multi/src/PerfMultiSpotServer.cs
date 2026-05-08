using System;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotServer
{
    private const uint RunId = 1;
    private const string Topic = "bench";

    internal static int Run(PerfOptions options)
    {
        SpotServerConfig config = BuildConfig(options);
        using var ctx = new Context();
        using var controlState = new RunnerControlState(config.Size);
        ApplyMultiServerContextOptions(ctx, options);
        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            config.ServiceName);
        using var nodePub = new SpotNode(ctx);
        using var spotPub = nodePub.CreateSpot();
        ConfigureSpotRegistryTlsIfNeeded(registry, config.Transport);
        registry.Bind(config.RegistryPubEndpoint, config.RegistryRouterEndpoint);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        ConfigureSpotDiscoveryTlsIfNeeded(discovery, config.Transport);
        discovery.ConnectRegistry(config.RegistryRouterEndpoint);

        ConfigureSpotNodeTlsIfNeeded(nodePub, config.Transport);
        ConfigureSpotNodePublisher(nodePub, options);
        nodePub.Bind(config.DataEndpoint);
        nodePub.AttachDiscovery(discovery);
        WriteStdoutLine(
            $"READY,{config.DataEndpoint}|{config.RegistryRouterEndpoint}");

        if (!controlState.WaitForStart(config.ReadyTimeoutMs))
        {
            if (!controlState.StopRequested)
                Console.Error.WriteLine("multi_server_error:no_start");
            return controlState.StopRequested ? 0 : 2;
        }

        return RunActivePhase(spotPub, controlState, config);
    }

    private static SpotServerConfig BuildConfig(PerfOptions options)
    {
        return new SpotServerConfig(
            options.Transport,
            Math.Max(1, options.Size),
            Math.Max(1, ResolveMultiDurationSeconds(options)),
            ResolveMultiConnectReadyTimeoutMs(options),
            ResolveMultiSpotRouteWarmupMs(),
            MultiEndpointFor(options.Transport, "multi-spot-data", options),
            MultiEndpointFor(options.Transport, "multi-spot-registry-pub", options),
            MultiEndpointFor(options.Transport, "multi-spot-registry-router", options));
    }

    private static void ConfigureSpotNodePublisher(SpotNode node,
        PerfOptions options)
    {
        int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        TrySetSpotOption(() => node.PubSubHighWaterMark = Math.Max(sndHwm, rcvHwm));
    }

    private static int RunActivePhase(Spot spotPub,
        RunnerControlState controlState, SpotServerConfig config)
    {
        ulong seq = 1;
        var payload = new byte[Math.Max(config.Size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        long warmupDeadlineTicks = config.RouteWarmupMs > 0
            ? DeadlineTicksFromMilliseconds(config.RouteWarmupMs)
            : Stopwatch.GetTimestamp();
        while (config.RouteWarmupMs > 0
               && !controlState.StopRequested
               && Stopwatch.GetTimestamp() < warmupDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Warmup,
                config.Size, seq++, EpochNs());
            TryPublish(spotPub, config, payload, SendFlags.DontWait);
            Thread.Sleep(5);
        }

        long activeDeadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, seq++, EpochNs());
            TryPublish(spotPub, config, payload, SendFlags.None);
        }

        for (int i = 0; i < 32 && !controlState.StopRequested; i++)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Cooldown,
                config.Size, seq++, EpochNs());
            TryPublish(spotPub, config, payload, SendFlags.None);
        }

        return 0;
    }

    private static bool TryPublish(Spot spotPub, SpotServerConfig config,
        byte[] payload, SendFlags flags)
    {
        try
        {
            using Message message = Message.FromBytes(payload);
            return spotPub.Publish(config.ServiceName, Topic)
                .Message(message)
                .Flags(flags)
                .Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
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

    private readonly struct SpotServerConfig
    {
        internal SpotServerConfig(string transport, int size,
            int durationSeconds, int readyTimeoutMs, int routeWarmupMs,
            string dataEndpoint, string registryPubEndpoint,
            string registryRouterEndpoint)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            ReadyTimeoutMs = readyTimeoutMs;
            RouteWarmupMs = routeWarmupMs;
            DataEndpoint = dataEndpoint;
            RegistryPubEndpoint = registryPubEndpoint;
            RegistryRouterEndpoint = registryRouterEndpoint;
            ServiceName = MultiSpotServiceName(registryRouterEndpoint);
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int ReadyTimeoutMs { get; }
        internal int RouteWarmupMs { get; }
        internal string DataEndpoint { get; }
        internal string RegistryPubEndpoint { get; }
        internal string RegistryRouterEndpoint { get; }
        internal string ServiceName { get; }
    }
}
