using System;
using System.Diagnostics;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotServer
{
    private const uint RunId = 1;
    private const string Topic = "bench";
    private const string ControlTopic = "ctrl";

    internal static int Run(PerfOptions options)
    {
        SpotServerConfig config = BuildConfig(options);
        using var ctx = new Context();
        using var controlState = new RunnerControlState(config.Size);
        ApplyMultiServerContextOptions(ctx, options);

        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            config.ChannelName);
        using var nodePub = new SpotNode(ctx);
        using var spotPub = nodePub.CreateSpot();

        ConfigureSpotRegistryTlsIfNeeded(registry, config.Transport);
        registry.Bind(config.RegistryPubEndpoint, config.RegistryRouterEndpoint);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        ConfigureSpotDiscoveryTlsIfNeeded(discovery, config.Transport);
        discovery.ConnectRegistry(config.RegistryRouterEndpoint);

        ConfigureSpotNodeTlsIfNeeded(nodePub, config.Transport);
        ConfigureSpotNodePublisher(nodePub, options, config);
        nodePub.Bind(config.DataEndpoint);
        nodePub.AttachDiscovery(discovery);

        using var controlNode = new SpotNode(ctx);
        ConfigureSpotNodeTlsIfNeeded(controlNode, config.Transport);
        controlNode.Bind(config.ControlEndpoint);

        RecalculateAutoHwm(ctx);

        controlState.SetConnectControlCallback(peerEndpoint =>
        {
            try
            {
                controlNode.ConnectPeer(peerEndpoint);
                WriteStdoutLine($"CONTROL_CONNECTED,{peerEndpoint}");
            }
            catch (ZlinkException ex)
            {
                Console.Error.WriteLine($"[multi-spot-server] peer connect failed: {ex.Message}");
            }
        });

        WriteStdoutLine(
            $"READY,{config.DataEndpoint}|{config.RegistryRouterEndpoint}");
        WriteStdoutLine($"CONTROL_READY,{config.ControlEndpoint}");

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
            MultiEndpointFor(options.Transport, "multi-spot-registry-pub",
                options),
            MultiEndpointFor(options.Transport, "multi-spot-registry-router",
                options),
            MultiEndpointFor(options.Transport, "multi-spot-control", options));
    }

    private static void ConfigureSpotNodePublisher(SpotNode node,
        PerfOptions options, SpotServerConfig config)
    {
        if (ManualSocketOverridesEnabled())
        {
            int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
            int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
            TrySetSpotOption(() => node.PubSubHighWaterMark =
                Math.Max(1, Math.Max(sndHwm, rcvHwm)));
        }
        if (options.SpotXpubNoDrop > 0)
            TrySetSpotOption(() => node.PublisherNoDrop = true);
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(config.ReadyTimeoutMs));
    }

    private static int RunActivePhase(Spot spotPub,
        RunnerControlState controlState, SpotServerConfig config)
    {
        ulong seq = 1;
        var payload = new byte[Math.Max(config.Size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        long activeDeadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);
        bool sendPending = false;

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, seq, EpochNs());
            bool sent = TryPublish(spotPub, config, payload, SendFlags.DontWait);
            if (sent)
            {
                seq++;
                sendPending = false;
            }
            else
            {
                sendPending = true;
            }
        }

        _ = sendPending;

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
            return spotPub.Publish(Topic)
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
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(
                                            ex.InternalErrno))
        {
        }
    }

    private readonly struct SpotServerConfig
    {
        internal SpotServerConfig(string transport, int size,
            int durationSeconds, int readyTimeoutMs,
            string dataEndpoint, string registryPubEndpoint,
            string registryRouterEndpoint, string controlEndpoint)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            ReadyTimeoutMs = readyTimeoutMs;
            DataEndpoint = dataEndpoint;
            RegistryPubEndpoint = registryPubEndpoint;
            RegistryRouterEndpoint = registryRouterEndpoint;
            ControlEndpoint = controlEndpoint;
            ChannelName = MultiSpotChannelName(registryRouterEndpoint);
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int ReadyTimeoutMs { get; }
        internal string DataEndpoint { get; }
        internal string RegistryPubEndpoint { get; }
        internal string RegistryRouterEndpoint { get; }
        internal string ControlEndpoint { get; }
        internal string ChannelName { get; }
    }
}
