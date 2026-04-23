using System;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfMultiSpotServer
{
    private const uint RunId = 1;
    private const string ServiceName = "bench-svc";
    private const string Topic = "bench";

    internal static int Run(PerfOptions options)
    {
        SpotServerConfig config = BuildConfig(options);
        using var ctx = new Context();
        using var controlState = new RunnerControlState(config.Size);
        ApplyMultiServerContextOptions(ctx, options);
        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, ServiceType.Spot, ServiceName);
        using var nodePub = new SpotNode(ctx);
        using var spotPub = nodePub.CreateSpot();
        registry.Bind(config.RegistryPubEndpoint, config.RegistryRouterEndpoint);
        discovery.ConnectRegistry(config.RegistryRouterEndpoint);
        nodePub.AttachDiscovery(discovery);

        ConfigureSpotTlsPublisherIfNeeded(nodePub, config.Transport);
        ConfigureSpotNodePublisher(nodePub, options);
        nodePub.Bind(config.DataEndpoint);
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
            MultiEndpointFor(options.Transport, "multi-spot-data", options),
            MultiEndpointFor(options.Transport, "multi-spot-registry-pub", options),
            MultiEndpointFor(options.Transport, "multi-spot-registry-router", options));
    }

    private static void ConfigureSpotNodePublisher(SpotNode node,
        PerfOptions options)
    {
        int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        int sndTimeo = ResolveMultiSndTimeoutMs(options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);
        bool xpubNoDrop = options.SpotXpubNoDrop > 0;

        TrySetSpotOption(() => node.PublisherOptions.Linger = TimeSpan.Zero);
        TrySetSpotOption(() => node.PublisherOptions.SendHighWaterMark = sndHwm);
        TrySetSpotOption(() => node.PublisherOptions.SendTimeout =
            TimeSpan.FromMilliseconds(sndTimeo));
        TrySetSpotOption(() => node.PublisherOptions.NoDrop = xpubNoDrop);
        TrySetSpotOption(() => node.SubscriberOptions.Linger = TimeSpan.Zero);
        TrySetSpotOption(() => node.SubscriberOptions.ReceiveHighWaterMark = rcvHwm);
        TrySetSpotOption(() => node.SubscriberOptions.ReceiveTimeout =
            TimeSpan.FromMilliseconds(rcvTimeo));
    }

    private static int RunActivePhase(Spot spotPub,
        RunnerControlState controlState, SpotServerConfig config)
    {
        ulong seq = 1;
        var payload = new byte[Math.Max(config.Size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        long activeDeadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, seq++, EpochNs());
            try
            {
                using Message message = Message.FromBytes(payload);
                if (!spotPub.Publish(ServiceName, Topic, message,
                        SendFlags.DontWait))
                {
                    continue;
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                continue;
            }
        }

        for (int i = 0; i < 32 && !controlState.StopRequested; i++)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Cooldown,
                config.Size, seq++, EpochNs());
            try
            {
                using Message message = Message.FromBytes(payload);
                _ = spotPub.Publish(ServiceName, Topic, message, SendFlags.None);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
        }

        return 0;
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
            int durationSeconds, int readyTimeoutMs, string dataEndpoint,
            string registryPubEndpoint, string registryRouterEndpoint)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            ReadyTimeoutMs = readyTimeoutMs;
            DataEndpoint = dataEndpoint;
            RegistryPubEndpoint = registryPubEndpoint;
            RegistryRouterEndpoint = registryRouterEndpoint;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int ReadyTimeoutMs { get; }
        internal string DataEndpoint { get; }
        internal string RegistryPubEndpoint { get; }
        internal string RegistryRouterEndpoint { get; }
    }
}
