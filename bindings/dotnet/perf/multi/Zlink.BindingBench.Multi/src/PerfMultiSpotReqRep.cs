using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfMultiSpotReqRep
{
    private const string ReqRepPattern = "SPOT_REQREP";
    private const string SendSendPattern = "SPOT_SENDSEND";
    private const string Topic = "bench";
    private const uint RunId = 1;
    private const int SpotSocketTag = 0;
    private const int ControlDeadlineTag = int.MaxValue;
    private const int ReadyRepeatTag = int.MaxValue - 1;
    private const int ActiveDeadlineTag = int.MaxValue - 2;
    private const int SendSendDrainBatchLimit = 16384;
    private static int s_debugClientSendLogs;
    private static int s_debugClientReplyLogs;
    private static int s_debugClientHeaderLogs;
    private static int s_debugClientPhaseLogs;
    private static int s_debugServerRecvLogs;
    private static int s_debugServerReplyLogs;

    private enum SpotEchoMode
    {
        RequestReply,
        SendSend,
    }

    private readonly struct SpotEchoConfig
    {
        internal SpotEchoConfig(SpotEchoMode mode, string pattern,
            string serverNodeRoutingIdText, string serverSpotRoutingIdText,
            string dataEndpointName, string controlEndpointName,
            string clientDataEndpointName, string clientControlEndpointName)
        {
            Mode = mode;
            Pattern = pattern;
            ServerNodeRoutingId = RoutingId.From(
                Encoding.ASCII.GetBytes(serverNodeRoutingIdText));
            ServerSpotRoutingId = RoutingId.From(
                Encoding.ASCII.GetBytes(serverSpotRoutingIdText));
            DataEndpointName = dataEndpointName;
            ControlEndpointName = controlEndpointName;
            ClientDataEndpointName = clientDataEndpointName;
            ClientControlEndpointName = clientControlEndpointName;
        }

        internal SpotEchoMode Mode { get; }
        internal string Pattern { get; }
        internal RoutingId ServerNodeRoutingId { get; }
        internal RoutingId ServerSpotRoutingId { get; }
        internal string DataEndpointName { get; }
        internal string ControlEndpointName { get; }
        internal string ClientDataEndpointName { get; }
        internal string ClientControlEndpointName { get; }
    }

    private static readonly SpotEchoConfig ReqRepConfig = new(
        SpotEchoMode.RequestReply,
        ReqRepPattern,
        "SPOT-REQREP-SERVER-NODE",
        "SPOT-REQREP-SERVER-SPOT",
        "multi-spot-reqrep-data",
        "multi-spot-reqrep-control",
        "multi-spot-reqrep-client",
        "multi-spot-reqrep-ctrl-client");

    private static readonly SpotEchoConfig SendSendConfig = new(
        SpotEchoMode.SendSend,
        SendSendPattern,
        "SPOT-SENDSEND-SERVER-NODE",
        "SPOT-SENDSEND-SERVER-SPOT",
        "multi-spot-sendsend-data",
        "multi-spot-sendsend-control",
        "multi-spot-sendsend-client",
        "multi-spot-sendsend-ctrl-client");

    internal static int RunServer(PerfOptions options)
        => RunServer(options, ReqRepConfig);

    internal static int RunClient(PerfOptions options)
        => RunClient(options, ReqRepConfig);

    internal static int RunSendSendServer(PerfOptions options)
        => RunServer(options, SendSendConfig);

    internal static int RunSendSendClient(PerfOptions options)
        => RunClient(options, SendSendConfig);

    private static int RunServer(PerfOptions options, SpotEchoConfig config)
    {
        int size = Math.Max(1, options.Size);
        int readyTimeoutMs = ResolveSpotServerReadyTimeoutMs(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int expectedReadyCount = ResolveMultiClients(options);
        string dataEndpoint = MultiEndpointFor(options.Transport,
            config.DataEndpointName, options);
        string dataRouterEndpoint = EndpointFor(options.Transport,
            config.DataEndpointName + "-router");
        string controlEndpoint = MultiEndpointFor(options.Transport,
            config.ControlEndpointName, options);

        using var ctx = Zlink.CreateContext();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);
        ApplyAutoHwmMsgUnit(ctx, size);

        using var dataNode = ctx.CreateSpotNode();
        using var replier = dataNode.CreateSpot();
        ApplyMultiSpotSocketOptions(replier, options);
        using var controlNode = ctx.CreateSpotNode();
        using var controlPub = controlNode.CreateSpot();
        using var controlSub = controlNode.CreateSpot();
        var serverStop = new ServerStopFlag();

        try
        {
            ConfigureSpotNodeTlsIfNeeded(dataNode, options.Transport);
            ConfigureSpotNodeTlsIfNeeded(controlNode, options.Transport);
            ConfigureDataNodeOptions(dataNode, options);
            ConfigureControlNodeOptions(controlNode, readyTimeoutMs);
            dataNode.SetRoutingId(config.ServerNodeRoutingId);
            replier.SetRoutingId(config.ServerSpotRoutingId);
            controlSub.SetSubscription(Topic);

            dataNode.SetRouterBind(dataRouterEndpoint);
            dataNode.SetPubBind(dataEndpoint);
            dataEndpoint = dataNode.LastEndpoint;
            controlNode.SetPubBind(controlEndpoint);
            controlEndpoint = controlNode.LastEndpoint;
            RecalculateAutoHwm(ctx);
            PrintSpotNodeAutoHwmSnapshot(dataNode, "spotnode_data_pub",
                options.Transport, size);
            PrintSpotNodeAutoHwmSnapshot(controlNode, "spotnode_control_pub",
                options.Transport, size);

            controlState.SetConnectControlCallback(peerEndpoint =>
            {
                try
                {
                    controlNode.ConnectPeer(peerEndpoint);
                    if (!WaitForConnectedPeerEndpoint(controlNode, peerEndpoint,
                            readyTimeoutMs))
                    {
                        Console.Error.WriteLine(
                            "multi_server_error:control_peer_timeout");
                        return;
                    }
                    WriteStdoutLine($"CONTROL_CONNECTED,{peerEndpoint}");
                }
                catch (ZlinkException ex)
                {
                    Console.Error.WriteLine(
                        $"multi_server_error:control_connect:{ex.Message}");
                }
            });

            replier.SetDispatchHandler(info =>
            {
                if (info.Event == SpotDispatchEvent.RoutedReadable
                    && !serverStop.IsSet)
                    DrainRoutedMessages(replier, config.Mode, serverStop);
            });

            WriteStdoutLine($"READY,{dataEndpoint}");
            WriteStdoutLine($"CONTROL_READY,{controlEndpoint}");

            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_server_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }

            var barrier = new ReadyBarrier(expectedReadyCount);
            if (!WaitForReadyBarrier(controlSub, dataNode, barrier, size,
                    readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_server_error:control_ready_timeout");
                return 2;
            }

            if (!WaitForConnectedPeers(dataNode, Math.Max(1, barrier.DataPeerCount),
                    readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_server_error:data_peer_timeout");
                return 2;
            }

            if (!PublishControlStartBurst(controlPub, size, readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_server_error:control_start_failed");
                return 2;
            }

            long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
            while (!controlState.StopRequested
                   && Stopwatch.GetTimestamp() < deadlineTicks)
            {
                Thread.Sleep(1);
            }

            serverStop.Set();
            Thread.Sleep(10);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"multi_server_error:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
        finally
        {
            serverStop.Set();
        }
    }

    private static int RunClient(PerfOptions options, SpotEchoConfig config)
    {
        int size = Math.Max(1, options.Size);
        int clientCount = ResolveMultiClients(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        string serverDataEndpoint = NormalizeClientEndpoint(options.Endpoint,
            options.Transport);
        string serverControlEndpoint = NormalizeClientEndpoint(
            options.ControlEndpoint, options.Transport);

        using var ctx = Zlink.CreateContext();
        using var controlState = new RunnerControlState(size);
        ApplyMultiClientContextOptions(ctx, options);
        ApplyAutoHwmMsgUnit(ctx, size);

        using var dataNode = ctx.CreateSpotNode();
        using var controlNode = ctx.CreateSpotNode();
        using var controlPub = controlNode.CreateSpot();
        using var controlSub = controlNode.CreateSpot();
        var slots = new List<ClientSlot>(clientCount);

        try
        {
            ConfigureSpotNodeTlsIfNeeded(dataNode, options.Transport);
            ConfigureSpotNodeTlsIfNeeded(controlNode, options.Transport);
            ConfigureDataNodeOptions(dataNode, options);
            ConfigureControlNodeOptions(controlNode, readyTimeoutMs);

            controlSub.SetSubscription(Topic);
            dataNode.SetRouterBind(EndpointFor(options.Transport,
                config.ClientDataEndpointName + "-router"));
            string dataEndpoint = BindSpotNodeWithRetry(dataNode,
                options.Transport, config.ClientDataEndpointName, options);
            dataNode.ConnectPeer(serverDataEndpoint);
            string controlEndpoint = BindSpotNodeWithRetry(controlNode,
                options.Transport, config.ClientControlEndpointName, options);
            if (!string.IsNullOrWhiteSpace(serverControlEndpoint))
            {
                controlNode.ConnectPeer(serverControlEndpoint);
                if (!WaitForConnectedPeerEndpoint(controlNode,
                        serverControlEndpoint, readyTimeoutMs))
                {
                    Console.Error.WriteLine(
                        "multi_client_error:server_control_peer_timeout");
                    return 2;
                }
            }
            for (int i = 0; i < clientCount; i++)
            {
                var requester = dataNode.CreateSpot();
                ApplyMultiSpotSocketOptions(requester, options);
                requester.SetRoutingId(RoutingId.From(
                    Encoding.ASCII.GetBytes(
                        config.Mode == SpotEchoMode.RequestReply
                            ? $"SPOT-REQREP-{i}"
                            : $"SPOT-SENDSEND-{i}")));
                var slot = new ClientSlot(requester);
                slots.Add(slot);
            }

            RecalculateAutoHwm(ctx);
            PrintSpotNodeAutoHwmSnapshot(dataNode, "spotnode_data_pub",
                options.Transport, size);
            PrintSpotNodeAutoHwmSnapshot(controlNode, "spotnode_control_pub",
                options.Transport, size);
            WriteStdoutLine($"CLIENT_CONTROL_ENDPOINT,{controlEndpoint}");

            if (!controlState.WaitForControlConnected(readyTimeoutMs))
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

            if (!PublishControlPayload(controlPub,
                    $"DATA_ENDPOINT,{dataEndpoint}", readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:data_endpoint_failed");
                return 2;
            }
            if (settleMs > 0)
                Thread.Sleep(settleMs);
            if (!WaitForConnectedPeers(dataNode, 1, readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:data_peer_timeout");
                return 2;
            }
            if (!PublishControlPayload(controlPub, "CONNECTED", readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:connected_failed");
                return 2;
            }
            if (settleMs > 0)
                Thread.Sleep(settleMs);
            if (!PublishControlPayload(controlPub,
                    $"READY_COUNT,{size},{clientCount}", readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:ready_count_failed");
                return 2;
            }

            WriteStdoutLine($"CLIENT_READY,{size}");

            if (!controlState.WaitForStart(readyTimeoutMs))
            {
                if (!controlState.StopRequested)
                    Console.Error.WriteLine("multi_client_error:no_start");
                return controlState.StopRequested ? 0 : 2;
            }
            if (!PublishControlPayload(controlPub,
                    $"DATA_ENDPOINT,{dataEndpoint}", readyTimeoutMs)
                || !PublishControlPayload(controlPub, "CONNECTED", readyTimeoutMs)
                || !PublishControlPayload(controlPub,
                    $"READY_COUNT,{size},{clientCount}", readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:ready_republish_failed");
                return 2;
            }
            if (!WaitForControlStart(controlSub, controlPub, dataEndpoint, size,
                    clientCount, readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:control_start_timeout");
                return 2;
            }

            int requestTimeoutMs = ResolveSpotReqRepRequestTimeoutMs(options);
            if (!RunActiveWindow(slots, size, durationSeconds,
                    requestTimeoutMs, config))
                return 2;

            return PrintMultiResult(config.Pattern, options.Transport, size,
                durationSeconds, slots);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"multi_client_error:{ex.GetType().Name}:{ex.Message}");
            if (PerfEnv.ReadPositive("PERF_DOTNET_CONTROL_DEBUG", 0) > 0)
                Console.Error.WriteLine(ex);
            return 2;
        }
        finally
        {
            for (int i = slots.Count - 1; i >= 0; i--)
                slots[i].Dispose();
        }
    }

    private static int ResolveSpotServerReadyTimeoutMs(PerfOptions options)
    {
        int connectReadyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        return Math.Max(connectReadyTimeoutMs,
            Math.Max(1000, connectReadyTimeoutMs * 6));
    }

    private static void ConfigureDataNodeOptions(ISpotNode node,
        PerfOptions options)
    {
        if (!ManualSocketOverridesEnabled())
            return;
        int hwm = Math.Max(options.ResolveMultiHwm("PERF_MULTI_SNDHWM"),
            options.ResolveMultiHwm("PERF_MULTI_RCVHWM"));
        TrySetSpotOption(() => node.RouterHighWaterMark = Math.Max(1, hwm));
    }

    private static void ConfigureControlNodeOptions(ISpotNode node, int timeoutMs)
    {
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs)));
    }

    private static bool WaitForReadyBarrier(ISpot controlSub, ISpotNode dataNode,
        ReadyBarrier barrier, int size, int timeoutMs)
    {
        string readyPrefix = $"READY_COUNT,{size},";
        using var deadlineTimer = Zlink.CreateTimer();
        using var poller = Zlink.CreatePoller();
        var events = new PollEvent[2];
        poller.Add(controlSub, PollEventFlags.PollIn, SpotSocketTag);
        poller.Add(deadlineTimer, ControlDeadlineTag);
        deadlineTimer.Start(TimeSpan.FromMilliseconds(Math.Max(1, timeoutMs)),
            1);

        while (true)
        {
            while (TryReceiveReadyBarrier(controlSub, dataNode, barrier,
                       readyPrefix, out bool ready))
            {
                if (ready)
                    return true;
            }

            if (!WaitForControlEvent(poller, events, deadlineTimer,
                    PollEventFlags.PollIn))
                return false;
        }
    }

    private static bool TryReceiveReadyBarrier(ISpot controlSub,
        ISpotNode dataNode, ReadyBarrier barrier, string readyPrefix,
        out bool ready)
    {
        ready = false;
        using var received = new TopicMessage();
        if (!controlSub.Subscribe(received, RecvFlags.DontWait))
            return false;
        if (received.Topic != Topic)
            return true;

        string payload = Encoding.ASCII.GetString(
            received.SinglePartOrThrow().AsReadOnlySpan());
        if (payload == "CONNECTED")
            barrier.ControlConnected = true;
        else if (payload.StartsWith("DATA_ENDPOINT,", StringComparison.Ordinal))
        {
            string endpoint = payload["DATA_ENDPOINT,".Length..];
            if (!string.IsNullOrWhiteSpace(endpoint))
            {
                dataNode.ConnectPeer(endpoint);
                barrier.DataPeerCount++;
            }
        }
        else if (payload.StartsWith(readyPrefix, StringComparison.Ordinal)
                 && int.TryParse(payload.AsSpan(readyPrefix.Length),
                     out int readyCount))
        {
            barrier.ReadyCount += Math.Max(0, readyCount);
        }

        ready = barrier.ControlConnected
                && barrier.ReadyCount >= barrier.ExpectedReadyCount;
        return true;
    }

    private static bool PublishControlStartBurst(ISpot controlSpot, int size,
        int timeoutMs)
    {
        return PublishControlPayload(controlSpot, $"START,{size}", timeoutMs);
    }

    private static bool WaitForControlStart(
        ISpot controlSub, ISpot controlPub,
        string dataEndpoint, int size, int readyCount, int timeoutMs)
    {
        string expected = $"START,{size}";
        if (!PublishReadyBarrier(controlPub, dataEndpoint, size, readyCount,
                timeoutMs))
            return false;

        using var deadlineTimer = Zlink.CreateTimer();
        using var repeatTimer = Zlink.CreateTimer();
        using var poller = Zlink.CreatePoller();
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

            if (!WaitForControlStartEvent(poller, events, controlPub,
                    dataEndpoint, size, readyCount, timeoutMs, deadlineTimer,
                    repeatTimer))
                return false;
        }
    }

    private static bool TryReceiveControlStart(ISpot controlSub, string expected,
        out bool started)
    {
        started = false;
        using var received = new TopicMessage();
        if (!controlSub.Subscribe(received, RecvFlags.DontWait))
            return false;
        if (received.Topic != Topic)
            return true;
        string payload = Encoding.ASCII.GetString(
            received.SinglePartOrThrow().AsReadOnlySpan());
        started = payload == expected;
        return true;
    }

    private static bool WaitForControlStartEvent(IPoller poller,
        PollEvent[] events, ISpot controlPub, string dataEndpoint, int size,
        int readyCount, int timeoutMs, Systems.Zlink.IZlinkTimer deadlineTimer,
        Systems.Zlink.IZlinkTimer repeatTimer)
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

                if (events[i].Slot == (nuint)ReadyRepeatTag)
                {
                    _ = repeatTimer.Recv();
                    if (!PublishReadyBarrier(controlPub, dataEndpoint, size,
                            readyCount, timeoutMs))
                        return false;
                    repeatTimer.Start(TimeSpan.FromMilliseconds(250), 1);
                    continue;
                }

                if (events[i].Slot == (nuint)SpotSocketTag
                    && (events[i].Revents & PollEventFlags.PollIn) != 0)
                    return true;
            }
        }
    }

    private static bool PublishReadyBarrier(ISpot controlPub, string dataEndpoint,
        int size, int readyCount, int timeoutMs)
    {
        return PublishControlPayload(controlPub, $"DATA_ENDPOINT,{dataEndpoint}",
                   timeoutMs)
               && PublishControlPayload(controlPub, "CONNECTED", timeoutMs)
               && PublishControlPayload(controlPub,
                   $"READY_COUNT,{size},{readyCount}", timeoutMs);
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
                using var message = new Message(bytes.AsSpan());
                if (controlSpot.Publish(Topic).Message(message)
                        .Flags(SendFlags.DontWait).Submit())
                    return true;
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

    private static bool WaitForConnectedPeers(ISpotNode node, int expectedCount,
        int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                if (node.Status().ConnectedPeerCount >= expectedCount)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno))
            {
            }
            Thread.Sleep(1);
        }

        return false;
    }

    private static bool WaitForConnectedPeerEndpoint(ISpotNode node,
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

    private static void DrainRoutedMessages(ISpot replier, SpotEchoMode mode,
        ServerStopFlag serverStop)
    {
        if (mode == SpotEchoMode.SendSend)
        {
            DrainSendSendRoutedMessages(replier, serverStop);
            return;
        }

        while (!serverStop.IsSet)
        {
            using var received = Received.Create();
            try
            {
                if (!replier.RecvRouted(received, RecvFlags.DontWait))
                    return;

                DebugLogLimited(ref s_debugServerRecvLogs,
                    $"spot_reqrep_server: recv request parts={received.Parts.Count} request_seq={received.RequestSeq ?? 0}");
                Message reply = received.FirstPart();
                if (mode == SpotEchoMode.RequestReply)
                {
                    received.Reply()
                        .Message(reply)
                        .Submit();
                    DebugLogLimited(ref s_debugServerReplyLogs,
                        "spot_reqrep_server: reply ok");
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno)
                                            || IsTransientSubmitErrno(ex.NativeErrno))
            {
                DebugLogLimited(ref s_debugServerReplyLogs,
                    $"spot_reqrep_server: transient errno={ex.NativeErrno}");
                return;
            }
        }
    }

    private static void DrainSendSendRoutedMessages(ISpot replier,
        ServerStopFlag serverStop)
    {
        using var received = Received.Create();
        int drained = 0;
        while (!serverStop.IsSet && drained < SendSendDrainBatchLimit)
        {
            try
            {
                if (!replier.RecvRouted(received, RecvFlags.DontWait))
                    return;
                drained++;
                if (received.RequestSeq != null || !received.IsSinglePart)
                    continue;
                if (received.RoutingId.HasValue && received.SpotRid.HasValue)
                {
                    _ = received.Send()
                        .Message(received.FirstPart())
                        .Flags(SendFlags.DontWait)
                        .Submit();
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno)
                                            || IsTransientSubmitErrno(ex.NativeErrno))
            {
                DebugLogLimited(ref s_debugServerReplyLogs,
                    $"spot_reqrep_server: transient errno={ex.NativeErrno}");
                return;
            }
        }
    }

    private static bool RunActiveWindow(List<ClientSlot> slots, int size,
        int durationSeconds, int requestTimeoutMs, SpotEchoConfig config)
    {
        Exception? failure = null;
        long activeDeadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
        for (int i = 0; i < slots.Count; i++)
        {
            slots[i].ResetForActive();
            Volatile.Write(ref slots[i].ActiveDeadlineTicks, activeDeadlineTicks);
            Volatile.Write(ref slots[i].ActiveMsgSize, size);
        }

        int activeSlots = ActiveSpotSlotLimit(slots.Count, size);
        using var activeTimer = Zlink.CreateTimer();
        using var sendPoller = Zlink.CreatePoller();
        var sendEvents = new PollEvent[Math.Max(1, activeSlots * 4 + 3)];
        for (int i = 0; i < activeSlots; i++)
        {
            if (config.Mode == SpotEchoMode.SendSend)
            {
                sendPoller.Add(slots[i].Requester,
                    PollEventFlags.PollIn, (nuint)i);
                slots[i].PollRegistered = true;
            }
            else
            {
                sendPoller.Add(slots[i].Requester,
                    PollEventFlags.PollCompletion, (nuint)i);
                slots[i].PollRegistered = true;
            }
        }
        sendPoller.Add(activeTimer, ActiveDeadlineTag);
        activeTimer.Start(TimeSpan.FromSeconds(Math.Max(1, durationSeconds)), 1);

        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            bool sendProgress = false;
            bool canSend = false;
            for (int i = 0; i < activeSlots; i++)
            {
                ClientSlot slot = slots[i];
                if (Volatile.Read(ref slot.WaitingReply) != 0)
                    continue;
                if (Volatile.Read(ref slot.SendPending) != 0)
                {
                    canSend = true;
                    continue;
                }

                canSend = true;
                try
                {
                    SendResult result = config.Mode == SpotEchoMode.RequestReply
                        ? TrySendRequest(slot, size, requestTimeoutMs, config)
                        : TrySendOneWay(slot, size, config);
                    if (result == SendResult.Sent)
                        sendProgress = true;
                    else if (result == SendResult.Blocked)
                    {
                        if (config.Mode == SpotEchoMode.SendSend)
                            Volatile.Write(ref slot.SendPending, 1);
                    }
                    else if (result == SendResult.Fatal)
                        return false;
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref failure, ex, null);
                    Console.Error.WriteLine(
                        $"multi_client_error:{ex.GetType().Name}:{ex.Message}");
                    return false;
                }
            }

            if (sendProgress)
                continue;

            if (canSend)
            {
                if (!WaitForSpotReqRepReady(sendPoller, sendEvents, slots,
                        activeSlots, size, activeDeadlineTicks, config,
                        activeTimer))
                    break;
            }
            else
            {
                if (!WaitForSpotReqRepReady(sendPoller, sendEvents, slots,
                        activeSlots, size, activeDeadlineTicks, config,
                        activeTimer))
                    break;
            }
        }

        DebugLogLimited(ref s_debugClientPhaseLogs,
            $"spot_reqrep_client: active loop end count={TotalMeasureCount(slots)} deadline={activeDeadlineTicks} now={Stopwatch.GetTimestamp()}");
        if (config.Mode == SpotEchoMode.RequestReply)
        {
            sendPoller.Remove(activeTimer);
            WaitForPendingReplies(sendPoller, sendEvents, slots, activeSlots,
                1000);
        }
        DebugLogLimited(ref s_debugClientPhaseLogs,
            $"spot_reqrep_client: pending wait end count={TotalMeasureCount(slots)}");

        for (int i = 0; i < slots.Count; i++)
        {
            Volatile.Write(ref slots[i].ActiveDeadlineTicks, 0);
        }

        if (failure == null)
            return true;

        Console.Error.WriteLine(
            $"multi_client_error:{failure.GetType().Name}:{failure.Message}");
        return false;
    }

    private static long TotalMeasureCount(List<ClientSlot> slots)
    {
        long count = 0;
        foreach (ClientSlot slot in slots)
            count += Volatile.Read(ref slot.MeasureCount);
        return count;
    }

    private enum SendResult
    {
        Sent,
        Blocked,
        NotConnected,
        Fatal,
    }

    private static SendResult TrySendRequest(ClientSlot slot, int size,
        int requestTimeoutMs, SpotEchoConfig config)
    {
        ulong seq = slot.NextSeq++;
        Message payload = slot.PreparePayload(size, seq);
        Volatile.Write(ref slot.WaitingReply, 1);
        bool submitted;
        try
        {
            submitted = slot.Requester
                .RequestToSpot(config.ServerNodeRoutingId,
                    config.ServerSpotRoutingId)
                .Message(payload)
                .Timeout(TimeSpan.FromMilliseconds(Math.Max(1, requestTimeoutMs)))
                .Flags(SendFlags.DontWait)
                .Submit((result, parts) =>
                    OnRequestReply(slot, result, parts, size));
        }
        catch (ZlinkSubmitException ex)
            when (ex.Result == ZlinkSubmitException.ErrorCode.NotConnected)
        {
            Volatile.Write(ref slot.WaitingReply, 0);
            Volatile.Write(ref slot.SendPending, 0);
            return SendResult.NotConnected;
        }
        if (submitted)
        {
            DebugLogLimited(ref s_debugClientSendLogs,
                $"spot_reqrep_client: send ok seq={seq}");
            return SendResult.Sent;
        }

        Volatile.Write(ref slot.WaitingReply, 0);
        Volatile.Write(ref slot.SendPending, 0);
        return SendResult.Blocked;
    }

    private static int ResolveSpotReqRepRequestTimeoutMs(PerfOptions options)
    {
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        if (rcvTimeoutMs > 0)
            return rcvTimeoutMs;

        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        return sndTimeoutMs > 0 ? sndTimeoutMs : 200;
    }

    private static SendResult TrySendOneWay(ClientSlot slot, int size,
        SpotEchoConfig config)
    {
        ulong seq = slot.NextSeq++;
        Message payload = slot.PreparePayload(size, seq);
        Volatile.Write(ref slot.WaitingReply, 1);
        bool submitted;
        try
        {
            submitted = slot.Requester
                .SendToSpot(config.ServerNodeRoutingId,
                    config.ServerSpotRoutingId)
                .Message(payload)
                .Flags(SendFlags.DontWait)
                .Submit();
        }
        catch (ZlinkSubmitException ex)
            when (ex.Result == ZlinkSubmitException.ErrorCode.NotConnected)
        {
            Volatile.Write(ref slot.WaitingReply, 0);
            Volatile.Write(ref slot.SendPending, 0);
            return SendResult.NotConnected;
        }
        if (submitted)
        {
            Volatile.Write(ref slot.SendPending, 0);
            return SendResult.Sent;
        }

        Volatile.Write(ref slot.WaitingReply, 0);
        Volatile.Write(ref slot.SendPending, 0);
        return SendResult.Blocked;
    }

    private static bool WaitForSpotReqRepReady(IPoller poller,
        PollEvent[] events, List<ClientSlot> slots, int activeSlots, int size,
        long activeDeadlineTicks, SpotEchoConfig config,
        Systems.Zlink.IZlinkTimer activeTimer)
    {
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= activeDeadlineTicks)
                return false;

            try
            {
                // Keep the wait signal-driven: socket readiness, callback wake,
                // or the active deadline timer must wake the poller.
                TimeSpan waitTimeout = config.Mode == SpotEchoMode.SendSend
                    ? TimeSpan.FromMilliseconds(50)
                    : Timeout.InfiniteTimeSpan;
                int written = poller.Wait(events, waitTimeout);
                if (written == 0 && config.Mode == SpotEchoMode.RequestReply)
                    return true;
                if (written == 0 && config.Mode == SpotEchoMode.SendSend)
                    return true;
                bool progressed = false;
                for (int i = 0; i < written; i++)
                {
                    if (events[i].Slot == (nuint)ActiveDeadlineTag)
                    {
                        _ = activeTimer.Recv();
                        return false;
                    }

                    nuint eventSlot = events[i].Slot;
                    if (eventSlot > (nuint)int.MaxValue
                        || (int)eventSlot >= activeSlots
                        || (int)eventSlot >= slots.Count)
                    {
                        continue;
                    }

                    int slotIndex = (int)eventSlot;
                    PollEventFlags revents = events[i].Revents;
                    if (config.Mode == SpotEchoMode.SendSend
                        && (revents & PollEventFlags.PollIn) != 0)
                    {
                        DrainSendSendReplies(slots[slotIndex], size,
                            activeDeadlineTicks);
                        progressed = true;
                    }
                    if (config.Mode == SpotEchoMode.RequestReply
                        && (revents & PollEventFlags.PollCompletion) != 0)
                    {
                        progressed = true;
                    }
                    if ((events[i].Revents
                            & PollEventFlags.PollOut)
                        != 0)
                    {
                        Volatile.Write(ref slots[slotIndex].SendPending, 0);
                        progressed = true;
                    }
                }

                if (progressed)
                    return true;
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
                    return true;
            }
        }
    }

    private static void OnRequestReply(ClientSlot slot, RequestResult result,
        IReadOnlyList<Message> replyParts, int size)
    {
        try
        {
            DebugLogLimited(ref s_debugClientReplyLogs,
                $"spot_reqrep_client: reply result={result} parts={replyParts.Count}");
            if (result != RequestResult.Ok || replyParts.Count == 0)
                return;

            ReadOnlySpan<byte> body = replyParts[0].AsReadOnlySpan();
            if (!PerfShared.TryDecodeMetricHeader(body,
                    out PerfMetricHeader header))
            {
                DebugLogLimited(ref s_debugClientHeaderLogs,
                    $"spot_reqrep_client: reply decode failed size={body.Length}");
                return;
            }

            RecordReply(slot, header, size);
        }
        finally
        {
            Volatile.Write(ref slot.WaitingReply, 0);
            Volatile.Write(ref slot.SendPending, 0);
            for (int i = 0; i < replyParts.Count; i++)
                replyParts[i].Dispose();
        }
    }

    private static void DrainSendSendReplies(ClientSlot slot, int size,
        long activeDeadlineTicks)
    {
        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            try
            {
                if (!slot.Requester.RecvRouted(slot.Received,
                        RecvFlags.DontWait))
                    return;

                if (slot.Received.RequestSeq != null
                    || !slot.Received.IsSinglePart)
                    continue;
                ReadOnlySpan<byte> body =
                    slot.Received.FirstPart().AsReadOnlySpan();
                if (PerfShared.TryDecodeMetricHeader(body,
                        out PerfMetricHeader header))
                {
                    Volatile.Write(ref slot.WaitingReply, 0);
                    Volatile.Write(ref slot.SendPending, 0);
                    RecordReply(slot, header, size);
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno))
            {
                return;
            }
        }
    }

    private static void DrainRequestReplyFallback(List<ClientSlot> slots,
        int activeSlots, int size, long activeDeadlineTicks)
    {
        int count = Math.Min(activeSlots, slots.Count);
        for (int i = 0; i < count; i++)
            DrainSendSendReplies(slots[i], size, activeDeadlineTicks);
    }

    private static void RecordReply(ClientSlot slot, PerfMetricHeader header,
        int size)
    {
        if (header.Phase != (uint)PerfPhase.Active
            || header.MsgSize != (uint)size
            || header.RunId != RunId)
        {
            DebugLogLimited(ref s_debugClientHeaderLogs,
                $"spot_reqrep_client: reply header mismatch run={header.RunId} phase={header.Phase} size={header.MsgSize} expected_size={size}");
            return;
        }

        Interlocked.Increment(ref slot.MeasureCount);
        DebugLogLimited(ref s_debugClientHeaderLogs,
            $"spot_reqrep_client: recorded reply seq={header.Seq} count={Volatile.Read(ref slot.MeasureCount)}");
        ulong nowNs = EpochNs();
        if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
        {
            slot.LatencySamples.Add((nowNs - header.SentTsNs) / 2.0);
        }
    }

    private static int ActiveSpotSlotLimit(int totalSlots, int msgSize)
    {
        if (msgSize >= 131072)
            return Math.Min(totalSlots, 8);
        if (msgSize >= 65536)
            return Math.Min(totalSlots, 32);
        return totalSlots;
    }

    private static void WaitForPendingReplies(IPoller poller, PollEvent[] events,
        List<ClientSlot> slots, int activeSlots, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool pending = false;
            int count = Math.Min(activeSlots, slots.Count);
            for (int i = 0; i < count; i++)
            {
                if (Volatile.Read(ref slots[i].WaitingReply) == 0)
                    continue;
                pending = true;
                break;
            }

            if (!pending)
                return;
            try
            {
                poller.Wait(events, TimeSpan.FromMilliseconds(50));
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.NativeErrno)
                                            || IsInterrupted(ex.NativeErrno))
            {
            }
        }
    }

    private static int PrintMultiResult(string pattern, string transport, int size,
        int durationSeconds, List<ClientSlot> slots)
    {
        long measureCount = 0;
        var samples = new List<double>();
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        foreach (ClientSlot slot in slots)
        {
            measureCount += Volatile.Read(ref slot.MeasureCount);
            for (int i = 0; i < slot.LatencySamples.Count; i++)
            {
                ReservoirSample(samples, slot.LatencySamples[i],
                    ref sampleSeen, int.MaxValue, ref rng);
            }
        }

        if (measureCount <= 0)
            return 2;

        double activeSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / activeSeconds;
        // PERF_POLICY: report measured latency only. C
        // normalize_latency_stats reports zeros when no samples and never
        // fabricates a duration-derived latency.
        var latency = ComputeLatencyStats(samples);
        double latencyNs = latency.mean;
        double latencyP95Ns = Math.Max(latency.p95, latencyNs);
        double latencyP99Ns = Math.Max(latency.p99, latencyP95Ns);
        PrintResult(pattern, transport, size, throughput, latencyNs,
            latencyP95Ns, latencyP99Ns);
        return 0;
    }

    private static bool IsTransientSubmitErrno(int errno)
    {
        return errno == 11 || errno == 110 || errno == 107 || errno == 113;
    }

    private static void DebugLogLimited(ref int counter, string message)
    {
        if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("PERF_DEBUG")))
            return;
        if (Interlocked.Increment(ref counter) > 8)
            return;
        Console.Error.WriteLine(message);
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

    private sealed class ReadyBarrier
    {
        internal ReadyBarrier(int expectedReadyCount)
        {
            ExpectedReadyCount = Math.Max(1, expectedReadyCount);
        }

        internal int ExpectedReadyCount { get; }
        internal int ReadyCount { get; set; }
        internal int DataPeerCount { get; set; }
        internal bool ControlConnected { get; set; }
    }

    private sealed class ServerStopFlag
    {
        private int _stopRequested;

        internal bool IsSet => Volatile.Read(ref _stopRequested) != 0;

        internal void Set()
        {
            Volatile.Write(ref _stopRequested, 1);
        }
    }

    private sealed class ClientSlot : IDisposable
    {
        internal ClientSlot(ISpot requester)
        {
            Requester = requester;
        }

        internal ISpot Requester { get; }
        internal List<double> LatencySamples { get; } = new();
        internal long MeasureCount;
        internal int WaitingReply;
        internal int SendPending;
        internal int ActiveMsgSize;
        internal long ActiveDeadlineTicks;
        internal ulong NextSeq = 1;
        internal bool PollRegistered;
        internal Received Received { get; } = Received.Create();
        private Message? _payload;
        private int _payloadSize;

        internal Message PreparePayload(int size, ulong seq)
        {
            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            if (_payload == null || _payloadSize != payloadSize)
            {
                _payload?.Dispose();
                _payload = Message.Allocate(payloadSize);
                _payloadSize = payloadSize;
                _payload.AsSpan().Fill((byte)'a');
            }

            StampMetricHeader(_payload.AsSpan(), RunId, PerfPhase.Active,
                size, seq, EpochNs());
            return _payload;
        }

        internal void ResetForActive()
        {
            MeasureCount = 0;
            WaitingReply = 0;
            SendPending = 0;
            ActiveMsgSize = 0;
            ActiveDeadlineTicks = 0;
            NextSeq = 1;
            PollRegistered = false;
            LatencySamples.Clear();
        }

        public void Dispose()
        {
            Received.Dispose();
            _payload?.Dispose();
            Requester.Dispose();
        }
    }

}
