using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotClient
{
    private const string Pattern = "SPOT";
    private const uint ExpectedRunId = 1;
    private const string Topic = "bench";
    private const string ControlTopic = Topic;
    private const int SpotSocketTag = 0;
    private const int ControlDeadlineTag = -1;
    private const int ReadyRepeatTag = -2;

    internal static int Run(PerfOptions options)
    {
        if (!TryParseSpotEndpoints(options.Endpoint, out string dataEndpoint,
                out string registryEndpoint))
        {
            Console.Error.WriteLine("multi_client_error:invalid_spot_ready_payload");
            return 1;
        }

        string channelName = MultiSpotChannelName(registryEndpoint);
        SpotClientConfig config = BuildConfig(options,
            NormalizeClientEndpoint(dataEndpoint, options.Transport),
            NormalizeClientEndpoint(registryEndpoint, options.Transport),
            channelName);
        using var ctx = new Context();
        using var controlState = new RunnerControlState(config.Size);
        ApplyMultiClientContextOptions(ctx, options);

        using var discovery = new Discovery(ctx, AutoConnectType.SpotMesh,
            config.ChannelName);
        using var node = new SpotNode(ctx);

        ConfigureSpotDiscoveryTlsIfNeeded(discovery, config.Transport);
        ConfigureSpotNodeTlsIfNeeded(node, config.Transport);
        ApplySpotSubscriberOptions(node, options);
        discovery.ConnectRegistry(config.RegistryEndpoint);
        BindSpotNodeWithRetry(node, config.Transport, "multi-spot-client",
            options);
        node.ConnectPeer(config.DataEndpoint);
        if (!WaitForConnectedPeerEndpoint(node, config.DataEndpoint,
                config.ConnectReadyTimeoutMs))
        {
            Console.Error.WriteLine("multi_client_error:data_peer_timeout");
            return 2;
        }

        RecalculateAutoHwm(ctx);
        PrintSpotNodeAutoHwmSnapshot(node, "spotnode_data_sub",
            config.Transport, config.Size);

        var slots = new List<SpotClientSlot>(config.ClientCount);
        try
        {
            for (int i = 0; i < config.ClientCount; i++)
            {
                var subscriber = node.CreateSpot();
                ApplyMultiSpotSocketOptions(subscriber, options);
                subscriber.SetSubscription(Topic);
                var slot = new SpotClientSlot(subscriber,
                    new SpotClientSlotState(config.LatencySampleCap,
                        config.LatencySampleStride));
                subscriber.OnDispatchEvent(info =>
                {
                    if (info.Event != SpotDispatchEvent.SubscribeReadable)
                        return;
                    long deadline = Interlocked.Read(
                        ref slot.State.ActiveDeadlineTicks);
                    int active = Volatile.Read(ref slot.State.Active);
                    int msgSize = Volatile.Read(ref slot.State.ActiveMsgSize);
                    if (active == 0 || deadline <= 0 || msgSize <= 0)
                        return;
                    DrainSlot(slot, msgSize, deadline);
                });
                slots.Add(slot);
            }

            using var controlNode = new SpotNode(ctx);
            ConfigureSpotNodeTlsIfNeeded(controlNode, config.Transport);
            ConfigureSpotControlNode(controlNode, config.ConnectReadyTimeoutMs);
            using var controlPub = controlNode.CreateSpot();
            using var controlSub = controlNode.CreateSpot();
            controlSub.SetSubscription(ControlTopic);
            string localControlEndpoint = BindSpotNodeWithRetry(controlNode,
                config.Transport, "multi-spot-ctrl-client", options);
            RecalculateAutoHwm(ctx);
            PrintSpotNodeAutoHwmSnapshot(controlNode, "spotnode_control_pub",
                config.Transport, config.Size);
            if (!string.IsNullOrEmpty(config.ServerControlEndpoint))
            {
                controlNode.ConnectPeer(config.ServerControlEndpoint);
                if (!WaitForConnectedPeerEndpoint(controlNode,
                        config.ServerControlEndpoint,
                        config.ConnectReadyTimeoutMs))
                {
                    Console.Error.WriteLine(
                        "multi_client_error:server_control_peer_timeout");
                    return 2;
                }
                DebugSubjects(controlNode, "client_after_server_connect");
            }
            if (!PublishControlPayload(controlPub, "CONNECTED",
                    config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:control_connected_failed");
                return 2;
            }

            WriteStdoutLine($"CLIENT_CONTROL_ENDPOINT,{localControlEndpoint}");

            if (!controlState.WaitForControlConnected(config.ConnectReadyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine(
                        "multi_client_error:control_not_connected");
                return controlState.StopRequested ? 0 : 2;
            }

            int stabilizeMs = ResolveMultiSpotControlStabilizeMs();
            int settleMs = ResolveMultiSpotControlSettleMs();
            if (stabilizeMs > 0)
                Thread.Sleep(stabilizeMs);
            if (settleMs > 0)
                Thread.Sleep(settleMs);

            if (!PublishReadyCount(controlPub, config.Size,
                    config.ClientCount, config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:control_ready_failed");
                return 2;
            }

            WriteStdoutLine($"CLIENT_READY,{config.Size}");

            if (!controlState.WaitForStart(config.ConnectReadyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }
            DebugSubjects(controlNode, "client_after_runner_start");
            if (!WaitForControlStart(controlSub, controlPub, config.Size,
                    config.ClientCount, config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:control_start_timeout");
                return 2;
            }

            return RunActivePhase(slots, controlState, config);
        }
        finally
        {
            DisposeSlots(slots);
        }
    }

    private static bool PublishReadyCount(Spot controlSpot, int size,
        int readyCount, int timeoutMs)
    {
        string payload = $"READY_COUNT,{size},{readyCount}";
        return PublishControlPayload(controlSpot, payload, timeoutMs);
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
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }

            if (!WaitForControlEvent(poller, events, PollEventFlags.PollOut))
                return false;
        }
    }

    private static bool WaitForControlStart(
        Spot controlSub, Spot controlPub,
        int size, int readyCount, int timeoutMs)
    {
        string expected = $"START,{size}";
        if (!PublishReadyCount(controlPub, size, readyCount, timeoutMs))
            return false;

        using var deadlineTimer = new Systems.Zlink.Timer();
        using var repeatTimer = new Systems.Zlink.Timer();
        using var poller = new Poller();
        var events = new PollEvent[3];
        poller.Add(controlSub, PollEventFlags.PollIn, SpotSocketTag);
        poller.Add(deadlineTimer, ControlDeadlineTag);
        poller.Add(repeatTimer, ReadyRepeatTag);
        deadlineTimer.Start(TimeSpan.FromMilliseconds(Math.Max(1, timeoutMs)),
            1);
        repeatTimer.Start(TimeSpan.FromMilliseconds(250), 1);

        while (true)
        {
            while (TryReceiveControlStart(controlSub, expected, out bool started))
            {
                if (started)
                    return true;
            }

            if (!WaitForControlStartEvent(poller, events, controlPub, size,
                    readyCount, timeoutMs, repeatTimer))
                return false;
        }
    }

    private static bool TryReceiveControlStart(Spot controlSub, string expected,
        out bool started)
    {
        started = false;
        using var received = new TopicMessage();
        if (!controlSub.Subscribe(received, RecvFlags.DontWait))
            return false;
        if (received.Topic != ControlTopic)
            return true;
        string payload = Encoding.ASCII.GetString(
            received.SinglePartOrThrow().AsReadOnlySpan());
        started = payload == expected;
        return true;
    }

    private static bool WaitForControlStartEvent(Poller poller,
        PollEvent[] events, Spot controlPub, int size, int readyCount,
        int timeoutMs, Systems.Zlink.Timer repeatTimer)
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

                if (events[i].Tag is ReadyRepeatTag)
                {
                    _ = events[i].Timer?.Recv();
                    if (!PublishReadyCount(controlPub, size, readyCount,
                            timeoutMs))
                        return false;
                    repeatTimer.Start(TimeSpan.FromMilliseconds(250), 1);
                    continue;
                }

                if (events[i].Tag is SpotSocketTag
                    && (events[i].Revents & PollEventFlags.PollIn) != 0)
                    return true;
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
                    return true;
            }
        }
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

    private static bool WaitForConnectedPeerEndpoint(SpotNode node,
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
            Thread.Sleep(1);
        }

        return false;
    }

    private static SpotClientConfig BuildConfig(PerfOptions options,
        string dataEndpoint, string registryEndpoint, string channelName)
    {
        return new SpotClientConfig(
            options.Transport,
            Math.Max(1, options.Size),
            Math.Max(1, ResolveMultiDurationSeconds(options)),
            ResolveMultiLatencySampleCap(options),
            ResolveMultiOnewayLatencySampleStride(),
            Math.Max(1, ResolveMultiClients(options)),
            ResolveMultiConnectReadyTimeoutMs(options),
            dataEndpoint,
            registryEndpoint,
            channelName,
            options.ControlEndpoint);
    }

    private static int RunActivePhase(List<SpotClientSlot> slots,
        RunnerControlState controlState, SpotClientConfig config)
    {
        _ = controlState;
        long activeDeadlineTicks = DeadlineTicksFromSeconds(
            config.DurationSeconds);
        long activeDurationNs = Math.Max(1L, config.DurationSeconds)
            * 1_000_000_000L;
        for (int i = 0; i < slots.Count; i++)
        {
            Volatile.Write(ref slots[i].State.ActiveMsgSize, config.Size);
            Interlocked.Exchange(ref slots[i].State.ActiveDurationNs,
                activeDurationNs);
            Interlocked.Exchange(ref slots[i].State.SenderWindowStartNs, 0);
            Interlocked.Exchange(ref slots[i].State.SenderWindowEndNs, 0);
            Interlocked.Exchange(ref slots[i].State.ActiveDeadlineTicks,
                activeDeadlineTicks);
            Volatile.Write(ref slots[i].State.Active, 1);
        }

        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
            Thread.Sleep(1);

        for (int i = 0; i < slots.Count; i++)
        {
            Volatile.Write(ref slots[i].State.Active, 0);
            DrainSlot(slots[i], config.Size, activeDeadlineTicks);
        }

        long measureCount = 0;
        var samples = new List<double>(Math.Max(0, config.LatencySampleCap));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        for (int i = 0; i < slots.Count; i++)
        {
            if (slots[i].State.Error != null)
            {
                Console.Error.WriteLine(
                    $"multi_client_error:spot_recv:{slots[i].State.Error}");
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
        // PERF_POLICY: report measured latency only. C
        // normalize_latency_stats reports zeros when no samples and never
        // fabricates a duration-derived latency.
        var latency = ComputeLatencyStats(samples);
        double latencyNs = latency.mean;
        double latencyP95Ns = Math.Max(latency.p95, latencyNs);
        double latencyP99Ns = Math.Max(latency.p99, latencyP95Ns);

        PrintResult(Pattern, config.Transport, config.Size, throughput,
            latencyNs, latencyP95Ns, latencyP99Ns);
        return 0;
    }

    private static bool DrainSlot(SpotClientSlot slot, int msgSize,
        long activeDeadlineTicks)
    {
        try
        {
            bool progressed = false;
            while (true)
            {
                if (Stopwatch.GetTimestamp() >= activeDeadlineTicks)
                    return progressed;

                using var subscribed = new TopicMessage();
                try
                {
                    if (!slot.Subscriber.Subscribe(subscribed, RecvFlags.DontWait))
                        return progressed;
                }
                catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                                || IsInterrupted(ex.InternalErrno))
                {
                    return progressed;
                }
                    progressed = true;
                    ReadOnlySpan<byte> payload =
                        subscribed.SinglePartOrThrow().AsReadOnlySpan();
                    if (IsStopTokenPayload(payload))
                    {
                        slot.State.CooldownSeen = 1;
                        return true;
                    }

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
                    if (header.Phase != (uint)PerfPhase.Active
                        || Stopwatch.GetTimestamp() > activeDeadlineTicks
                        || !IsWithinSenderWindow(slot.State, header))
                    {
                        continue;
                    }

                    slot.State.MeasureCount++;
                    if (slot.State.MeasureCount % slot.State.LatencySampleStride == 0
                        && header.SentTsNs > 0)
                    {
                        ulong nowNs = EpochNs();
                        if (nowNs >= header.SentTsNs)
                        {
                            double sampleLatencyNs = nowNs - header.SentTsNs;
                            ReservoirSample(slot.State.LatencySamples,
                                sampleLatencyNs, ref slot.State.SampleSeen,
                                slot.State.LatencySampleCap, ref slot.State.Rng);
                        }
                    }
            }
        }
        catch (Exception ex)
        {
            Interlocked.CompareExchange(ref slot.State.Error, ex, null);
            return false;
        }
    }

    private static bool IsWithinSenderWindow(SpotClientSlotState state,
        PerfMetricHeader header)
    {
        if (header.SentTsNs == 0)
            return true;

        long sentNs = unchecked((long)header.SentTsNs);
        long windowEndNs = Interlocked.Read(ref state.SenderWindowEndNs);
        if (windowEndNs == 0)
        {
            lock (state.SenderWindowLock)
            {
                windowEndNs = state.SenderWindowEndNs;
                if (windowEndNs == 0)
                {
                    long durationNs = Math.Max(1L, state.ActiveDurationNs);
                    long computedEndNs = sentNs > long.MaxValue - durationNs
                        ? long.MaxValue
                        : sentNs + durationNs;
                    state.SenderWindowStartNs = sentNs;
                    state.SenderWindowEndNs = computedEndNs;
                    windowEndNs = computedEndNs;
                }
            }
        }

        return sentNs <= windowEndNs;
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
        if (!ManualSocketOverridesEnabled())
            return;
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        TrySetSpotOption(() => node.PubSubHighWaterMark = Math.Max(1, rcvHwm));
    }

    private static void ConfigureSpotControlNode(SpotNode node, int timeoutMs)
    {
        TrySetSpotOption(() => node.PublisherNoDrop = true);
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs)));
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

    private static string NormalizeClientEndpoint(string endpoint,
        string transport)
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
            int durationSeconds, int latencySampleCap, int latencySampleStride,
            int clientCount, int connectReadyTimeoutMs,
            string dataEndpoint, string registryEndpoint, string channelName,
            string serverControlEndpoint)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            LatencySampleCap = latencySampleCap;
            LatencySampleStride = latencySampleStride;
            ClientCount = clientCount;
            ConnectReadyTimeoutMs = connectReadyTimeoutMs;
            DataEndpoint = dataEndpoint;
            RegistryEndpoint = registryEndpoint;
            ChannelName = channelName;
            ServerControlEndpoint = serverControlEndpoint;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int LatencySampleCap { get; }
        internal int LatencySampleStride { get; }
        internal int ClientCount { get; }
        internal int ConnectReadyTimeoutMs { get; }
        internal string DataEndpoint { get; }
        internal string RegistryEndpoint { get; }
        internal string ChannelName { get; }
        internal string ServerControlEndpoint { get; }
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
        internal SpotClientSlotState(int latencySampleCap,
            int latencySampleStride)
        {
            LatencySampleCap = Math.Max(0, latencySampleCap);
            LatencySampleStride = Math.Max(1, latencySampleStride);
            LatencySamples = new List<double>(LatencySampleCap);
            Rng = 0xC0FFEEu;
        }

        internal List<double> LatencySamples { get; }
        internal int LatencySampleCap { get; }
        internal int LatencySampleStride { get; }
        internal long SampleSeen;
        internal uint Rng;
        internal long MeasureCount;
        internal int CooldownSeen;
        internal int Active;
        internal int ActiveMsgSize;
        internal long ActiveDeadlineTicks;
        internal long ActiveDurationNs;
        internal long SenderWindowStartNs;
        internal long SenderWindowEndNs;
        internal readonly object SenderWindowLock = new();
        internal Exception? Error;

        public void Dispose()
        {
        }
    }
}
