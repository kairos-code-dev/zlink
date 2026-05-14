using System;
using System.Diagnostics;
using System.Text;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotServer
{
    private const uint RunId = 1;
    private const string Topic = "bench";
    private const string ControlTopic = Topic;
    private const int SpotSocketTag = 0;
    private const int ActiveDeadlineTag = -1;
    private const int ControlDeadlineTag = -2;

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
        ApplyMultiSpotSocketOptions(spotPub, options);

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
        ConfigureSpotControlNode(controlNode, config.ReadyTimeoutMs);
        using var controlPub = controlNode.CreateSpot();
        using var controlSub = controlNode.CreateSpot();
        controlSub.SetSubscription(ControlTopic);
        controlNode.Bind(config.ControlEndpoint);
        string actualDataEndpoint = nodePub.LastEndpoint;
        string actualControlEndpoint = controlNode.LastEndpoint;

        RecalculateAutoHwm(ctx);
        PrintSpotNodeAutoHwmSnapshot(nodePub, "spotnode_data_pub",
            config.Transport, config.Size);
        PrintSpotNodeAutoHwmSnapshot(controlNode, "spotnode_control_pub",
            config.Transport, config.Size);

        controlState.SetConnectControlCallback(peerEndpoint =>
        {
            try
            {
                controlNode.ConnectPeer(peerEndpoint);
                if (!WaitForControlPeerConnected(controlNode,
                        peerEndpoint, config.ReadyTimeoutMs))
                {
                    Console.Error.WriteLine(
                        "[multi-spot-server] peer connect ready timeout");
                    return;
                }
                WriteStdoutLine($"CONTROL_CONNECTED,{peerEndpoint}");
            }
            catch (ZlinkException ex)
            {
                Console.Error.WriteLine($"[multi-spot-server] peer connect failed: {ex.Message}");
            }
        });

        WriteStdoutLine(
            $"READY,{actualDataEndpoint}|{config.RegistryRouterEndpoint}");
        WriteStdoutLine($"CONTROL_READY,{actualControlEndpoint}");

        if (!controlState.WaitForStart(config.ReadyTimeoutMs))
        {
            if (!controlState.StopRequested)
                Console.Error.WriteLine("multi_server_error:no_start");
            return controlState.StopRequested ? 0 : 2;
        }
        DebugControl("server_runner_start");
        if (!WaitForReadyCount(controlSub, config.Size,
                ResolveMultiClients(options), config.ReadyTimeoutMs))
        {
            Console.Error.WriteLine("multi_server_error:control_ready_timeout");
            return 2;
        }
        DebugControl("server_ready_count");
        DebugSubjects(controlNode, "server_before_start");
        if (!PublishControlStartBurst(controlPub, config.Size,
                config.ReadyTimeoutMs))
        {
            Console.Error.WriteLine("multi_server_error:control_start_failed");
            return 2;
        }
        DebugControl("server_control_start");

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

    private static void ConfigureSpotControlNode(SpotNode node, int timeoutMs)
    {
        TrySetSpotOption(() => node.PublisherNoDrop = true);
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs)));
    }

    private static int RunActivePhase(Spot spotPub,
        RunnerControlState controlState, SpotServerConfig config)
    {
        ulong seq = 1;
        var payload = new byte[Math.Max(config.Size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        long activeDeadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);
        using var activeTimer = new Systems.Zlink.Timer();
        using var sendPoller = new Poller();
        var sendEvents = new PollEvent[2];
        sendPoller.Add(spotPub, PollEventFlags.PollOut, SpotSocketTag);
        sendPoller.Add(activeTimer, ActiveDeadlineTag);
        activeTimer.Start(TimeSpan.FromSeconds(
            Math.Max(1, config.DurationSeconds)), 1);

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, seq, EpochNs());
            if (TryPublish(spotPub, config, payload, SendFlags.DontWait))
            {
                seq++;
                continue;
            }

            if (!WaitForSpotPublishReady(sendPoller, sendEvents,
                    ActiveDeadlineTag))
                break;
        }

        // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via wire-level
        // stop token. Multiple retries cover transient publisher
        // backpressure so every subscriber observes the terminator.
        if (!controlState.StopRequested)
            TryPublishStopToken(spotPub, config);

        return 0;
    }

    private static void TryPublishStopToken(Spot spotPub, SpotServerConfig config)
    {
        TryPublish(spotPub, config, MultiStopToken, SendFlags.None);
    }

    private static bool WaitForReadyCount(Spot controlSpot, int size,
        int expectedCount, int timeoutMs)
    {
        int readyCount = 0;
        string prefix = $"READY_COUNT,{size},";
        using var deadlineTimer = new Systems.Zlink.Timer();
        using var poller = new Poller();
        var events = new PollEvent[2];
        poller.Add(controlSpot, PollEventFlags.PollIn, SpotSocketTag);
        poller.Add(deadlineTimer, ControlDeadlineTag);
        deadlineTimer.Start(TimeSpan.FromMilliseconds(Math.Max(1, timeoutMs)),
            1);

        while (true)
        {
            while (TryReceiveReadyCount(controlSpot, prefix, ref readyCount,
                       expectedCount, out bool ready))
            {
                if (ready)
                    return true;
            }

            if (!WaitForControlEvent(poller, events, PollEventFlags.PollIn))
                return false;
        }
    }

    private static bool TryReceiveReadyCount(Spot controlSpot, string prefix,
        ref int readyCount, int expectedCount, out bool ready)
    {
        ready = false;
        using TopicMessage? received = controlSpot.Subscribe(RecvFlags.DontWait);
        if (received == null)
            return false;
        if (received.Topic != ControlTopic)
            return true;

        string payload = Encoding.ASCII.GetString(
            received.SinglePartOrThrow().AsReadOnlySpan());
        if (!payload.StartsWith(prefix, StringComparison.Ordinal))
            return true;
        if (!int.TryParse(payload.AsSpan(prefix.Length), out int increment))
            return true;
        readyCount += Math.Max(0, increment);
        ready = readyCount >= expectedCount;
        return true;
    }

    private static bool PublishControlStart(Spot controlSpot, int size,
        int timeoutMs)
    {
        string payload = $"START,{size}";
        return PublishControlPayload(controlSpot, payload, timeoutMs);
    }

    private static bool PublishControlStartBurst(Spot controlSpot, int size,
        int timeoutMs)
    {
        return PublishControlStart(controlSpot, size, timeoutMs);
    }

    private static bool PublishControlPayload(Spot controlSpot, string payload,
        int timeoutMs)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(payload);
        using var deadlineTimer = new Systems.Zlink.Timer();
        using var poller = new Poller();
        var events = new PollEvent[2];
        poller.Add(controlSpot, PollEventFlags.PollOut, SpotSocketTag);
        poller.Add(deadlineTimer, ControlDeadlineTag);
        deadlineTimer.Start(TimeSpan.FromMilliseconds(Math.Max(1, timeoutMs)),
            1);

        while (true)
        {
            try
            {
                using Message message = new(bytes.AsSpan());
                if (controlSpot.Publish(ControlTopic).Message(message)
                        .Flags(SendFlags.DontWait).Submit())
                {
                    DebugControl($"publish:{payload}");
                    return true;
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }

            if (!WaitForControlEvent(poller, events, PollEventFlags.PollOut))
                return false;
        }
    }

    private static bool WaitForControlPeerConnected(SpotNode node,
        string peerEndpoint, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            SpotNodePeerEntry[] peers = node.PeersQuery(new SpotNodePeerFilter(
                PeerEndpoint: peerEndpoint,
                State: SpotPeerState.Connected));
            if (peers.Length > 0)
                return true;
            System.Threading.Thread.Sleep(1);
        }

        return false;
    }

    private static void DebugControl(string message)
    {
        if (PerfEnv.ReadPositive("PERF_DOTNET_CONTROL_DEBUG", 0) > 0)
            Console.Error.WriteLine($"control_debug:{message}");
    }

    private static void DebugSubjects(SpotNode node, string label)
    {
        if (PerfEnv.ReadPositive("PERF_DOTNET_CONTROL_DEBUG", 0) <= 0)
            return;
        SpotNodeStatus status = node.StatusSnapshot();
        Console.Error.WriteLine(
            $"control_debug:{label}:subjects={status.SubjectCount}:ready={status.ReadySubjectCount}:peers={status.ConnectedPeerCount}");
        foreach (SpotNodeSubjectEntry entry in node.SubjectsSnapshot())
        {
            Console.Error.WriteLine(
                $"control_debug:{label}:subject:{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}:{entry.ActivePeerCount}");
        }
    }

    private static bool TryPublish(Spot spotPub, SpotServerConfig config,
        byte[] payload, SendFlags flags)
    {
        try
        {
            using Message message = new(payload.AsSpan());
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

    private static bool WaitForSpotPublishReady(Poller poller,
        PollEvent[] events, int deadlineTag)
    {
        while (true)
        {
            try
            {
                int written = poller.Wait(events,
                    TimeSpan.FromMilliseconds(MultiClientPollTimeoutMs),
                    out _);
                for (int i = 0; i < written; i++)
                {
                    if (events[i].Tag is int tag && tag == deadlineTag)
                    {
                        _ = events[i].Timer?.Recv();
                        return false;
                    }

                    if (events[i].Tag is SpotSocketTag
                        && (events[i].Revents & PollEventFlags.PollOut) != 0)
                    {
                        return true;
                    }
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
        }
    }

    private static bool WaitForControlEvent(Poller poller, PollEvent[] events,
        PollEventFlags required)
    {
        while (true)
        {
            int written = poller.Wait(events,
                TimeSpan.FromMilliseconds(MultiClientPollTimeoutMs), out _);
            for (int i = 0; i < written; i++)
            {
                if (events[i].Tag is ControlDeadlineTag)
                {
                    _ = events[i].Timer?.Recv();
                    return false;
                }

                if (events[i].Tag is SpotSocketTag
                    && (events[i].Revents & required) != 0)
                {
                    return true;
                }
            }
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
