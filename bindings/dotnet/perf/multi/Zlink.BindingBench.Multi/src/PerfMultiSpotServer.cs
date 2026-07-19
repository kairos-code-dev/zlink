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
    private const int ActiveDeadlineTag = int.MaxValue;
    private const int ControlDeadlineTag = int.MaxValue - 1;

    internal static int Run(PerfOptions options)
    {
        SpotServerConfig config = BuildConfig(options);
        using var ctx = Zlink.CreateContext();
        using var controlState = new RunnerControlState(config.Size);
        ApplyMultiServerContextOptions(ctx, options);
        ApplyAutoHwmMsgUnit(ctx, config.Size);

        using var registry = ctx.CreateRegistry();
        using var discovery = ctx.CreateDiscovery(AutoConnectType.SpotMesh, config.ChannelName);
        using var nodePub = ctx.CreateSpotNode();
        using var spotPub = nodePub.CreateSpot();
        ApplyMultiSpotSocketOptions(spotPub, options);

        ConfigureSpotRegistryTlsIfNeeded(registry, config.Transport);
        registry.Bind(config.RegistryPubEndpoint, config.RegistryRouterEndpoint);
        registry.SetBroadcastInterval(TimeSpan.FromMilliseconds(50));
        ConfigureSpotDiscoveryTlsIfNeeded(discovery, config.Transport);
        // ITEM 3 fix: the ONLY real defect was the unresolved wildcard
        // ("tcp://127.0.0.1:*") registry endpoint, which made
        // discovery.ConnectRegistry fail with errno 22 (EINVAL) and the
        // whole MULTI_SPOT pattern be misreported as UNSUPPORTED. Discovery
        // connects to the registry ROUTER endpoint (matching the working
        // samples/DiscoveryRegistry sample); endpoints are now concrete.
        ConnectRegistryWithRetry(discovery, config.RegistryRouterEndpoint,
            config.ReadyTimeoutMs);

        ConfigureSpotNodeTlsIfNeeded(nodePub, config.Transport);
        ConfigureSpotNodePublisher(nodePub, options, config);
        string actualDataEndpoint = BindSpotNodeWithRetry(nodePub,
            config.Transport, "multi-spot-data", options);
        nodePub.AttachDiscovery(discovery);

        using var controlNode = ctx.CreateSpotNode();
        ConfigureSpotNodeTlsIfNeeded(controlNode, config.Transport);
        ConfigureSpotControlNode(controlNode, config.ReadyTimeoutMs);
        using var controlPub = controlNode.CreateSpot();
        using var controlSub = controlNode.CreateSpot();
        controlSub.SetSubscription(ControlTopic);
        string actualControlEndpoint = BindSpotNodeWithRetry(controlNode,
            config.Transport, "multi-spot-control", options);

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

        // ITEM 3 fix: advertise the registry ROUTER endpoint (what the
        // client's discovery.ConnectRegistry connects to), now concrete.
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
            ResolveSpotServerReadyTimeoutMs(options),
            MultiEndpointFor(options.Transport, "multi-spot-data", options),
            // ITEM 3 fix: registry endpoints must be CONCRETE (not "*") so
            // the bound address can be reused for discovery.ConnectRegistry
            // (server side) and advertised to the client via READY.
            MultiRegistryEndpoint(options.Transport, options),
            MultiRegistryEndpoint(options.Transport, options),
            MultiEndpointFor(options.Transport, "multi-spot-control", options));
    }

    private static int ResolveSpotServerReadyTimeoutMs(PerfOptions options)
    {
        int connectReadyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        return Math.Max(connectReadyTimeoutMs,
            Math.Max(1000, connectReadyTimeoutMs * 6));
    }

    private static void ConfigureSpotNodePublisher(ISpotNode node,
        PerfOptions options, SpotServerConfig config)
    {
        if (ManualSocketOverridesEnabled())
        {
            int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
            int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
            TrySetSpotOption(() => node.PubSubHighWaterMark =
                Math.Max(1, Math.Max(sndHwm, rcvHwm)));
        }
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(config.ReadyTimeoutMs));
    }

    private static void ConfigureSpotControlNode(ISpotNode node, int timeoutMs)
    {
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs)));
    }

    // C parity: bindings/c/perf/multi/src/perf_multi_spot_server.cpp:197.
    // PERF_MULTI_SPOT_LATENCY_ONLY enables a paced (one message per
    // interval) active publish so the second comparison pass measures
    // uncontended round-trip latency instead of the saturated live pass.
    private static bool ResolveSpotLatencyOnlyMode()
    {
        string? value = Environment.GetEnvironmentVariable(
            "PERF_MULTI_SPOT_LATENCY_ONLY");
        return !string.IsNullOrEmpty(value) && value != "0";
    }

    // C parity: resolve_spot_latency_only_interval_us() default 1000us.
    private static int ResolveSpotLatencyOnlyIntervalUs()
    {
        return PerfEnv.ReadPositive(
            "PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US", 1000);
    }

    private static int RunActivePhase(ISpot spotPub,
        RunnerControlState controlState, SpotServerConfig config)
    {
        ulong seq = 1;

        bool latencyOnly = ResolveSpotLatencyOnlyMode();
        long probeIntervalTicks = latencyOnly
            ? (long)(ResolveSpotLatencyOnlyIntervalUs()
                     * (Stopwatch.Frequency / 1_000_000.0))
            : 0;
        long nextProbeTicks = Stopwatch.GetTimestamp();

        long activeDeadlineTicks = DeadlineTicksFromSeconds(config.DurationSeconds);
        using var activeTimer = Zlink.CreateTimer();
        using var sendPoller = Zlink.CreatePoller();
        var sendEvents = new PollEvent[2];
        sendPoller.Add(spotPub, PollEventFlags.PollOut, SpotSocketTag);
        sendPoller.Add(activeTimer, ActiveDeadlineTag);
        activeTimer.Start(TimeSpan.FromSeconds(
            Math.Max(1, config.DurationSeconds)), 1);

        while (!controlState.StopRequested
               && Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            // C parity (perf_multi_spot_server.cpp:350): in latency-only
            // mode, sleep until the next paced send slot so each probe
            // travels over an idle link.
            if (latencyOnly)
            {
                long now = Stopwatch.GetTimestamp();
                if (now < nextProbeTicks)
                {
                    long waitTicks = nextProbeTicks - now;
                    long waitMs = Math.Max(1,
                        waitTicks * 1000 / Stopwatch.Frequency);
                    System.Threading.Thread.Sleep(
                        (int)Math.Min(waitMs, int.MaxValue));
                    continue;
                }
            }

            if (TryPublishActive(spotPub, config, seq, SendFlags.DontWait)
                || TryPublishActive(spotPub, config, seq, SendFlags.None))
            {
                seq++;
                if (latencyOnly)
                    nextProbeTicks = Stopwatch.GetTimestamp()
                        + probeIntervalTicks;
                continue;
            }

            if (!WaitForSpotPublishReady(sendPoller, sendEvents,
                    activeTimer, ActiveDeadlineTag))
                break;
        }

        // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via wire-level
        // stop token. Multiple retries cover transient publisher
        // backpressure so every subscriber observes the terminator.
        if (!controlState.StopRequested)
            TryPublishStopToken(spotPub, config);

        return 0;
    }

    private static void TryPublishStopToken(ISpot spotPub, SpotServerConfig config)
    {
        TryPublish(spotPub, config, MultiStopToken, SendFlags.None);
    }

    private static bool TryPublishActive(ISpot spotPub, SpotServerConfig config,
        ulong seq, SendFlags flags)
    {
        try
        {
            using Message message = Message.Allocate(Math.Max(config.Size,
                PerfMetricHeaderSize));
            StampMetricHeader(message.AsSpan(), RunId, PerfPhase.Active,
                config.Size, seq, EpochNs());
            return spotPub.Publish(Topic).Message(message).Flags(flags)
                .Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                        || IsInterrupted(ex.NativeErrno))
        {
            return false;
        }
    }

    private static bool WaitForReadyCount(ISpot controlSpot, int size,
        int expectedCount, int timeoutMs)
    {
        int readyCount = 0;
        string prefix = $"READY_COUNT,{size},";
        using var deadlineTimer = Zlink.CreateTimer();
        using var poller = Zlink.CreatePoller();
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

            if (!WaitForControlEvent(poller, events, deadlineTimer,
                    PollEventFlags.PollIn))
                return false;
        }
    }

    private static bool TryReceiveReadyCount(ISpot controlSpot, string prefix,
        ref int readyCount, int expectedCount, out bool ready)
    {
        ready = false;
        using var received = new TopicMessage();
        if (!controlSpot.Subscribe(received, RecvFlags.DontWait))
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

    private static bool PublishControlStart(ISpot controlSpot, int size,
        int timeoutMs)
    {
        string payload = $"START,{size}";
        return PublishControlPayload(controlSpot, payload, timeoutMs);
    }

    private static bool PublishControlStartBurst(ISpot controlSpot, int size,
        int timeoutMs)
    {
        return PublishControlStart(controlSpot, size, timeoutMs);
    }

    private static bool PublishControlPayload(ISpot controlSpot, string payload,
        int timeoutMs)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(payload);
        using var deadlineTimer = Zlink.CreateTimer();
        using var poller = Zlink.CreatePoller();
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
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno))
            {
            }

            if (!WaitForControlEvent(poller, events, deadlineTimer,
                    PollEventFlags.PollOut))
                return false;
        }
    }

    private static bool WaitForControlPeerConnected(ISpotNode node,
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

    private static void DebugSubjects(ISpotNode node, string label)
    {
        if (PerfEnv.ReadPositive("PERF_DOTNET_CONTROL_DEBUG", 0) <= 0)
            return;
        SpotNodeStatus status = node.Status();
        Console.Error.WriteLine(
            $"control_debug:{label}:subjects={status.SubjectCount}:ready={status.ReadySubjectCount}:peers={status.ConnectedPeerCount}");
        foreach (SpotNodeSubjectEntry entry in node.Subjects())
        {
            Console.Error.WriteLine(
                $"control_debug:{label}:subject:{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}:{entry.ActivePeerCount}");
        }
    }

    private static bool TryPublish(ISpot spotPub, SpotServerConfig config,
        byte[] payload, SendFlags flags)
    {
        try
        {
            using Message message = new(payload.AsSpan());
            return spotPub.Publish(Topic).Message(message).Flags(flags)
                .Submit();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                        || IsInterrupted(ex.NativeErrno))
        {
            return false;
        }
    }

    private static bool WaitForSpotPublishReady(IPoller poller,
        PollEvent[] events, Systems.Zlink.IZlinkTimer activeTimer, int deadlineTag)
    {
        while (true)
        {
            try
            {
                int written = poller.Wait(events,
                    TimeSpan.FromMilliseconds(MultiClientPollTimeoutMs));
                for (int i = 0; i < written; i++)
                {
                    if (events[i].Slot == (nuint)deadlineTag)
                    {
                        _ = activeTimer.Recv();
                        return false;
                    }

                    if (events[i].Slot == (nuint)SpotSocketTag
                        && (events[i].Revents & PollEventFlags.PollOut) != 0)
                    {
                        return true;
                    }
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno))
            {
            }
        }
    }

    private static bool WaitForControlEvent(IPoller poller, PollEvent[] events,
        Systems.Zlink.IZlinkTimer deadlineTimer, PollEventFlags required)
    {
        while (true)
        {
            int written = poller.Wait(events,
                TimeSpan.FromMilliseconds(MultiClientPollTimeoutMs));
            for (int i = 0; i < written; i++)
            {
                if (events[i].Slot == (nuint)ControlDeadlineTag)
                {
                    _ = deadlineTimer.Recv();
                    return false;
                }

                if (events[i].Slot == (nuint)SpotSocketTag
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
                                            ex.NativeErrno))
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
