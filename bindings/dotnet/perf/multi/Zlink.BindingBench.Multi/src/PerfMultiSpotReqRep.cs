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
            ServerNodeRoutingId = RoutingId.FromBytes(
                Encoding.ASCII.GetBytes(serverNodeRoutingIdText));
            ServerSpotRoutingId = RoutingId.FromBytes(
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
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int expectedReadyCount = ResolveMultiClients(options);
        string dataEndpoint = MultiEndpointFor(options.Transport,
            config.DataEndpointName, options);
        string controlEndpoint = MultiEndpointFor(options.Transport,
            config.ControlEndpointName, options);

        using var ctx = new Context();
        using var controlState = new RunnerControlState(size);
        ApplyMultiServerContextOptions(ctx, options);

        using var dataNode = new SpotNode(ctx);
        using var replier = dataNode.CreateSpot();
        ApplyMultiSpotSocketOptions(replier, options);
        using var controlNode = new SpotNode(ctx);
        using var controlPub = controlNode.CreateSpot();
        using var controlSub = controlNode.CreateSpot();

        try
        {
            ConfigureSpotNodeTlsIfNeeded(dataNode, options.Transport);
            ConfigureSpotNodeTlsIfNeeded(controlNode, options.Transport);
            ConfigureDataNodeOptions(dataNode, options);
            ConfigureControlNodeOptions(controlNode, readyTimeoutMs);
            dataNode.SetRoutingId(config.ServerNodeRoutingId);
            replier.SetRoutingId(config.ServerSpotRoutingId);
            controlSub.SetSubscription(Topic);

            dataNode.Bind(dataEndpoint);
            dataEndpoint = dataNode.LastEndpoint;
            controlNode.Bind(controlEndpoint);
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

            replier.OnDispatchEvent(info =>
            {
                if (info.Event == SpotDispatchEvent.RoutedReadable)
                    DrainRoutedMessages(replier, config.Mode);
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

            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(
                $"multi_server_error:{ex.GetType().Name}:{ex.Message}");
            return 2;
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

        using var ctx = new Context();
        using var controlState = new RunnerControlState(size);
        ApplyMultiClientContextOptions(ctx, options);

        using var dataNode = new SpotNode(ctx);
        using var controlNode = new SpotNode(ctx);
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
            using var controlStart = new PerfSpotControlStartWatcher(controlSub,
                Topic, size);

            for (int i = 0; i < clientCount; i++)
            {
                var requester = dataNode.CreateSpot();
                ApplyMultiSpotSocketOptions(requester, options);
                requester.SetRoutingId(RoutingId.FromBytes(
                    Encoding.ASCII.GetBytes(
                        config.Mode == SpotEchoMode.RequestReply
                            ? $"SPOT-REQREP-{i}"
                            : $"SPOT-SENDSEND-{i}")));
                var slot = new ClientSlot(requester);
                if (config.Mode == SpotEchoMode.RequestReply)
                {
                    requester.OnDispatchEvent(info =>
                    {
                        if (info.Event == SpotDispatchEvent.ChannelReplyReadable)
                        {
                            try
                            {
                                info.DrainChannelReply();
                            }
                            catch (ZlinkException ex) when (
                                IsWouldBlock(ex.InternalErrno)
                                || IsInterrupted(ex.InternalErrno)
                                || ex.InternalErrno == 16)
                            {
                            }
                        }
                    });
                }
                else
                {
                    requester.OnDispatchEvent(info =>
                    {
                        if (info.Event != SpotDispatchEvent.RoutedReadable)
                            return;
                        long deadline =
                            Volatile.Read(ref slot.ActiveDeadlineTicks);
                        int msgSize = Volatile.Read(ref slot.ActiveMsgSize);
                        if (deadline <= 0 || msgSize <= 0)
                            return;
                        DrainSendSendReplies(slot, msgSize, deadline);
                    });
                }
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
            if (!WaitForControlStart(controlStart, controlPub, dataEndpoint, size,
                    clientCount, readyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:control_start_timeout");
                return 2;
            }

            if (!RunActiveWindow(slots, size, durationSeconds, readyTimeoutMs,
                    config))
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

    private static void ConfigureDataNodeOptions(SpotNode node,
        PerfOptions options)
    {
        if (!ManualSocketOverridesEnabled())
            return;
        int hwm = Math.Max(options.ResolveMultiHwm("PERF_MULTI_SNDHWM"),
            options.ResolveMultiHwm("PERF_MULTI_RCVHWM"));
        TrySetSpotOption(() => node.RouterHighWaterMark = Math.Max(1, hwm));
    }

    private static void ConfigureControlNodeOptions(SpotNode node, int timeoutMs)
    {
        TrySetSpotOption(() => node.PublisherNoDrop = true);
        TrySetSpotOption(() =>
            node.PublisherSendTimeout =
                TimeSpan.FromMilliseconds(Math.Max(1000, timeoutMs)));
    }

    private static bool WaitForReadyBarrier(Spot controlSub, SpotNode dataNode,
        ReadyBarrier barrier, int size, int timeoutMs)
    {
        string readyPrefix = $"READY_COUNT,{size},";
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            using TopicMessage? received = controlSub.Subscribe(RecvFlags.DontWait);
            if (received == null)
            {
                Thread.Yield();
                continue;
            }
            if (received.Topic != Topic)
                continue;

            string payload = Encoding.ASCII.GetString(
                received.SinglePartOrThrow().AsReadOnlySpan());
            if (payload == "CONNECTED")
            {
                barrier.ControlConnected = true;
                continue;
            }
            if (payload.StartsWith("DATA_ENDPOINT,", StringComparison.Ordinal))
            {
                string endpoint = payload["DATA_ENDPOINT,".Length..];
                if (!string.IsNullOrWhiteSpace(endpoint))
                {
                    dataNode.ConnectPeer(endpoint);
                    barrier.DataPeerCount++;
                }
                continue;
            }
            if (payload.StartsWith(readyPrefix, StringComparison.Ordinal)
                && int.TryParse(payload.AsSpan(readyPrefix.Length),
                    out int readyCount))
            {
                barrier.ReadyCount += Math.Max(0, readyCount);
            }

            if (barrier.ControlConnected
                && barrier.ReadyCount >= barrier.ExpectedReadyCount)
            {
                return true;
            }
        }

        return false;
    }

    private static bool PublishControlStartBurst(Spot controlSpot, int size,
        int timeoutMs)
    {
        return PublishControlPayload(controlSpot, $"START,{size}", timeoutMs);
    }

    private static bool WaitForControlStart(
        PerfSpotControlStartWatcher controlStart, Spot controlPub,
        string dataEndpoint, int size, int readyCount, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        long nextReadyTicks = 0;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= nextReadyTicks)
            {
                if (nextReadyTicks == 0
                    && !PublishReadyBarrier(controlPub, dataEndpoint, size,
                        readyCount, timeoutMs))
                {
                    return false;
                }
                nextReadyTicks = nowTicks + (Stopwatch.Frequency / 4);
            }

            if (controlStart.Wait(1))
                return true;
        }

        return false;
    }

    private static bool PublishReadyBarrier(Spot controlPub, string dataEndpoint,
        int size, int readyCount, int timeoutMs)
    {
        return PublishControlPayload(controlPub, $"DATA_ENDPOINT,{dataEndpoint}",
                   timeoutMs)
               && PublishControlPayload(controlPub, "CONNECTED", timeoutMs)
               && PublishControlPayload(controlPub,
                   $"READY_COUNT,{size},{readyCount}", timeoutMs);
    }

    private static bool PublishControlPayload(Spot controlSpot, string payload,
        int timeoutMs)
    {
        byte[] bytes = Encoding.ASCII.GetBytes(payload);
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                using var message = new Message(bytes.AsSpan());
                if (controlSpot.Publish(Topic).Message(message).Submit())
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
            Thread.Yield();
        }

        return false;
    }

    private static bool WaitForConnectedPeers(SpotNode node, int expectedCount,
        int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                if (node.StatusSnapshot().ConnectedPeerCount >= expectedCount)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
            Thread.Sleep(1);
        }

        return false;
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

    private static void DrainRoutedMessages(Spot replier, SpotEchoMode mode)
    {
        while (true)
        {
            Received? received = null;
            try
            {
                received = replier.RecvRouted(RecvFlags.DontWait);
                if (received == null)
                    return;
                using (received)
                {
                    Message reply = received.FirstPart();
                    if (mode == SpotEchoMode.RequestReply)
                    {
                        received.Reply().Message(reply)
                            .Flags(SendFlags.DontWait).Submit();
                    }
                    else
                    {
                        RoutingId? sourceNode = received.RoutingId;
                        RoutingId? sourceSpot = received.SpotRid;
                        if (sourceNode.HasValue && sourceSpot.HasValue)
                        {
                            _ = replier.SendToSpot(sourceNode.Value,
                                    sourceSpot.Value)
                                .Message(reply)
                                .Flags(SendFlags.DontWait)
                                .Submit();
                        }
                    }
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno)
                                            || IsTransientSubmitErrno(ex.InternalErrno))
            {
                received?.Dispose();
                return;
            }
        }
    }

    private static bool RunActiveWindow(List<ClientSlot> slots, int size,
        int durationSeconds, int readyTimeoutMs, SpotEchoConfig config)
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
        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            bool sendProgress = false;
            for (int i = 0; i < activeSlots; i++)
            {
                ClientSlot slot = slots[i];
                if (Volatile.Read(ref slot.WaitingReply) != 0
                    || Volatile.Read(ref slot.SendPending) != 0)
                {
                    continue;
                }

                try
                {
                    SendResult result = config.Mode == SpotEchoMode.RequestReply
                        ? TrySendRequest(slot, size, readyTimeoutMs, config)
                        : TrySendOneWay(slot, size, config);
                    if (result == SendResult.Sent)
                        sendProgress = true;
                    else if (result == SendResult.Fatal)
                        return false;
                }
                catch (Exception ex)
                {
                    Interlocked.CompareExchange(ref failure, ex, null);
                    return false;
                }
            }

            if (sendProgress)
                continue;

            Thread.Yield();
        }

        for (int i = 0; i < slots.Count; i++)
            Volatile.Write(ref slots[i].ActiveDeadlineTicks, 0);

        if (failure == null)
            return true;

        Console.Error.WriteLine(
            $"multi_client_error:{failure.GetType().Name}:{failure.Message}");
        return false;
    }

    private enum SendResult
    {
        Sent,
        Blocked,
        NotConnected,
        Fatal,
    }

    private static SendResult TrySendRequest(ClientSlot slot, int size,
        int readyTimeoutMs, SpotEchoConfig config)
    {
        ulong seq = slot.NextSeq++;
        using var payload = new Message(CreatePayload(size, seq).AsSpan());
        Volatile.Write(ref slot.WaitingReply, 1);
        bool submitted = slot.Requester
            .RequestToSpot(config.ServerNodeRoutingId, config.ServerSpotRoutingId)
            .Message(payload)
            .Timeout(TimeSpan.FromMilliseconds(Math.Max(1, readyTimeoutMs)))
            .Flags(SendFlags.DontWait)
            .Submit((result, parts) =>
                OnRequestReply(slot, result, parts, size));
        if (submitted)
            return SendResult.Sent;

        Volatile.Write(ref slot.WaitingReply, 0);
        Volatile.Write(ref slot.SendPending, 1);
        return SendResult.Blocked;
    }

    private static SendResult TrySendOneWay(ClientSlot slot, int size,
        SpotEchoConfig config)
    {
        ulong seq = slot.NextSeq++;
        using var payload = new Message(CreatePayload(size, seq).AsSpan());
        Volatile.Write(ref slot.WaitingReply, 1);
        bool submitted = slot.Requester
            .SendToSpot(config.ServerNodeRoutingId, config.ServerSpotRoutingId)
            .Message(payload)
            .Flags(SendFlags.DontWait)
            .Submit();
        if (submitted)
        {
            Volatile.Write(ref slot.SendPending, 0);
            return SendResult.Sent;
        }

        Volatile.Write(ref slot.WaitingReply, 0);
        Volatile.Write(ref slot.SendPending, 1);
        return SendResult.Blocked;
    }

    private static void OnRequestReply(ClientSlot slot, RequestResult result,
        IReadOnlyList<Message> replyParts, int size)
    {
        try
        {
            Volatile.Write(ref slot.WaitingReply, 0);
            Volatile.Write(ref slot.SendPending, 0);
            if (result != RequestResult.Ok || replyParts.Count == 0)
                return;

            ReadOnlySpan<byte> body = replyParts[0].AsReadOnlySpan();
            if (PerfShared.TryDecodeMetricHeader(body,
                    out PerfMetricHeader header))
            {
                RecordReply(slot, header, size);
            }
        }
        finally
        {
            for (int i = 0; i < replyParts.Count; i++)
                replyParts[i].Dispose();
        }
    }

    private static void DrainSendSendReplies(ClientSlot slot, int size,
        long activeDeadlineTicks)
    {
        while (Stopwatch.GetTimestamp() < activeDeadlineTicks)
        {
            Received? received = null;
            try
            {
                received = slot.Requester.RecvRouted(RecvFlags.DontWait);
                if (received == null)
                    return;
                using (received)
                {
                    if (received.RequestSeq != null)
                        continue;
                    ReadOnlySpan<byte> body =
                        received.FirstPart().AsReadOnlySpan();
                    if (PerfShared.TryDecodeMetricHeader(body,
                            out PerfMetricHeader header))
                    {
                        Volatile.Write(ref slot.WaitingReply, 0);
                        Volatile.Write(ref slot.SendPending, 0);
                        RecordReply(slot, header, size);
                    }
                }
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                received?.Dispose();
                return;
            }
        }
    }

    private static void RecordReply(ClientSlot slot, PerfMetricHeader header,
        int size)
    {
        if (header.Phase != (uint)PerfPhase.Active
            || header.MsgSize != (uint)size
            || header.RunId != RunId)
        {
            return;
        }

        Interlocked.Increment(ref slot.MeasureCount);
        ulong nowNs = EpochNs();
        if (header.SentTsNs > 0 && nowNs >= header.SentTsNs)
        {
            lock (slot.LatencySamples)
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
            lock (slot.LatencySamples)
            {
                for (int i = 0; i < slot.LatencySamples.Count; i++)
                {
                    ReservoirSample(samples, slot.LatencySamples[i],
                        ref sampleSeen, int.MaxValue, ref rng);
                }
            }
        }

        if (measureCount <= 0)
            return 2;

        double activeSeconds = Math.Max(1.0, durationSeconds);
        double throughput = measureCount / activeSeconds;
        double fallbackLatencyNs = (activeSeconds * 1_000_000_000.0)
            / Math.Max(1.0, measureCount * 2.0);
        var latency = ComputeLatencyStats(samples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;
        PrintResult(pattern, transport, size, throughput, latencyNs,
            latencyP95Ns, latencyP99Ns);
        return 0;
    }

    private static byte[] CreatePayload(int size, ulong seq)
    {
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');
        StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Active, size, seq,
            EpochNs());
        return payload;
    }

    private static bool IsTransientSubmitErrno(int errno)
    {
        return errno == 11 || errno == 110 || errno == 107 || errno == 113;
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

    private sealed class ClientSlot : IDisposable
    {
        internal ClientSlot(Spot requester)
        {
            Requester = requester;
        }

        internal Spot Requester { get; }
        internal List<double> LatencySamples { get; } = new();
        internal long MeasureCount;
        internal int WaitingReply;
        internal int SendPending;
        internal int ActiveMsgSize;
        internal long ActiveDeadlineTicks;
        internal ulong NextSeq = 1;

        internal void ResetForActive()
        {
            MeasureCount = 0;
            WaitingReply = 0;
            SendPending = 0;
            ActiveMsgSize = 0;
            ActiveDeadlineTicks = 0;
            NextSeq = 1;
            lock (LatencySamples)
                LatencySamples.Clear();
        }

        public void Dispose()
        {
            Requester.Dispose();
        }
    }
}
