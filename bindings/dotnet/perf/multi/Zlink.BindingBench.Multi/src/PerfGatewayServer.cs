using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using Zlink.Service;
using static PerfRunner;

internal static class PerfGatewayServer
{
    private const string Pattern = "GATEWAY";
    private const string ServiceName = "perf-gateway";
    private const string ServerGatewayRoutingId = "sg";
    private const uint ExpectedRunId = 1;

    internal static int Run(string transport, int size)
    {
        GatewayServerConfig config = BuildConfig(transport, size);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        Registry? registry = null;
        Receiver? receiver = null;
        Discovery? discovery = null;
        Gateway? gateway = null;
        try
        {
            registry = new Registry(ctx);
            registry.SetHeartbeat(5000, 60000);
            registry.SetEndpoints(config.RegistryPub, config.RegistryRouter);
            ApplyRegistrySocketOptions(registry, Pattern, config.SndTimeoutMs,
                config.RcvTimeoutMs);
            registry.Start();

            receiver = new Receiver(ctx);
            ConfigureReceiverTlsServerIfNeeded(receiver, config.Transport);
            receiver.Bind(config.Endpoint);
            ConnectRegistryWithRetry(() =>
                receiver.ConnectRegistry(config.RegistryRouter));
            receiver.Register(ServiceName, config.Endpoint, 1);
            ApplyReceiverSocketOptions(receiver, Pattern, config.SndTimeoutMs,
                config.RcvTimeoutMs);

            discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            ConnectRegistryWithRetry(() =>
                discovery.ConnectRegistry(config.RegistryRouter));
            for (int i = 0; i < config.ClientCount; i++)
                discovery.Subscribe($"c{i}");

            gateway = new Gateway(ctx, discovery, ServerGatewayRoutingId);
            ConfigureGatewayTlsClientIfNeeded(gateway, config.Transport);
            gateway.SetOption(SocketOptions.SndHwm,
                ResolveMultiHwmValue("PERF_SNDHWM", Pattern));
            gateway.SetOption(SocketOptions.RcvHwm,
                ResolveMultiHwmValue("PERF_RCVHWM", Pattern));
            gateway.SetOption(SocketOptions.Linger, 0);
            gateway.SetOption(SocketOptions.SndTimeo, config.SndTimeoutMs);
            gateway.SetOption(SocketOptions.RcvTimeo, config.RcvTimeoutMs);

            Console.WriteLine(
                $"READY,{config.Endpoint}|{config.RegistryPub}|{config.RegistryRouter}");
            _ = WaitUntil(() => receiver.GetRouterPeers().Length > 0,
                config.ReadyTimeoutMs);

            var routingId = new byte[256];
            var payload = new byte[Math.Max(256, Math.Max(Math.Max(config.Size,
                PerfMetricHeaderSize), MultiStopToken.Length))];
            Gateway.PreparedService[] clientServices = BuildClientServices(
                gateway, config.ClientCount);
            GatewayServerResult result = RunActivePhase(receiver, gateway,
                routingId, payload, clientServices, config);
            if (result.HasValue)
            {
                PrintResult(Pattern, config.Transport, config.Size,
                    result.Throughput, result.LatencyUs, result.LatencyP95Us,
                    result.LatencyP99Us);
            }

            return 0;
        }
        finally
        {
            TryDisposeQuietly(gateway);
            TryDisposeQuietly(discovery);
            TryDisposeQuietly(receiver);
            TryDisposeQuietly(registry);
        }
    }

    private static GatewayServerConfig BuildConfig(string transport, int size)
    {
        int resolvedSize = Math.Max(1, size);
        return new GatewayServerConfig(
            transport,
            resolvedSize,
            ResolveMultiWarmupSeconds(),
            ResolveMultiDurationSeconds(),
            ResolveMultiSettleMs(),
            ResolveMultiSndTimeoutMs(),
            ResolveMultiRcvTimeoutMs(),
            ResolveMultiConnectReadyTimeoutMs(),
            ResolveMultiClients(Pattern),
            ResolveMultiLatencySampleCap(),
            MultiEndpointFor(transport, "multi-gateway"),
            EndpointFor("tcp", "multi-gateway-reg-pub"),
            EndpointFor("tcp", "multi-gateway-reg-router"));
    }

    private static GatewayServerResult RunActivePhase(Receiver receiver,
        Gateway gateway, byte[] routingId, byte[] payload,
        Gateway.PreparedService[] clientServices,
        GatewayServerConfig config)
    {
        long recvCount = 0;
        long benchStartTicks = 0;
        long benchEndTicks = 0;
        long activeHardStopTicks = 0;
        int settleSeconds = (config.SettleMs + 999) / 1000;
        long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(6, config.WarmupSeconds + settleSeconds + 3)
            * Stopwatch.Frequency;
        long lastActiveMessageTicks = 0;
        long idleBreakTicks = (long)(Stopwatch.Frequency
            * (Math.Max(config.RcvTimeoutMs * 2, 1000) / 1000.0));
        var latencySamples = new List<double>(config.LatencySampleCap);
        PendingGatewayReply[] pendingReplies =
            CreatePendingReplies(config.ClientCount, payload.Length);
        int pendingReplyCount = 0;
        using var poller = new Poller();
        var events = new List<PollEvent>(2);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        poller.AddReceiver(receiver, PollEvents.PollIn, "receiver");
        poller.AddGateway(gateway, PollEvents.None, "gateway");

        while (true)
        {
            FlushPendingReplies(gateway, pendingReplies, ref pendingReplyCount);
            poller.ModifyGateway(gateway,
                pendingReplyCount > 0 ? PollEvents.PollOut : PollEvents.None);

            if (activeHardStopTicks > 0
                && Stopwatch.GetTimestamp() >= activeHardStopTicks)
                break;

            int pollTimeoutMs = pendingReplyCount > 0 ? 0 : 2;
            if (!WaitForEvents(poller, events, pollTimeoutMs))
            {
                long nowTicks = Stopwatch.GetTimestamp();
                if (activeHardStopTicks > 0 && nowTicks >= activeHardStopTicks)
                    break;
                if (lastActiveMessageTicks > 0)
                {
                    if (nowTicks - lastActiveMessageTicks >= idleBreakTicks)
                        break;
                }
                else if (nowTicks >= firstPacketDeadlineTicks)
                {
                    break;
                }
                continue;
            }

            bool drainedAny = false;
            while (TryReceiveGatewayRequest(receiver, routingId, payload,
                       out int routingIdLength, out int n))
            {
                drainedAny = true;
                ReadOnlySpan<byte> body = payload.AsSpan(0, n);
                if (IsStopTokenPayload(body))
                    goto Done;

                if (!TryParseClientIndex(routingId.AsSpan(0, routingIdLength),
                        clientServices.Length,
                        out int clientIndex))
                    continue;
                EnqueueOrSendGatewayReply(gateway, pendingReplies,
                    ref pendingReplyCount, clientServices[clientIndex], body);
                if (!TryDecodeMetricHeader(body, out PerfMetricHeader header))
                    continue;
                if (header.RunId != ExpectedRunId
                    || header.MsgSize != (uint)config.Size
                    || header.Phase != (uint)PerfPhase.Active)
                    continue;

                if (benchStartTicks == 0)
                {
                    benchStartTicks = Stopwatch.GetTimestamp();
                    activeHardStopTicks = benchStartTicks
                        + (long)Math.Max(2, config.DurationSeconds + 1)
                        * Stopwatch.Frequency;
                }

                benchEndTicks = Stopwatch.GetTimestamp();
                lastActiveMessageTicks = benchEndTicks;
                recvCount++;
                ulong nowUs = EpochUs();
                if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                {
                    double sampleUs = nowUs - header.SentTsUs;
                    ReservoirSample(latencySamples, sampleUs, ref sampleSeen,
                        config.LatencySampleCap, ref rng);
                }
            }

            if (drainedAny)
                continue;

            long idleNowTicks = Stopwatch.GetTimestamp();
            if (activeHardStopTicks > 0 && idleNowTicks >= activeHardStopTicks)
                break;
            if (lastActiveMessageTicks > 0
                && idleNowTicks - lastActiveMessageTicks >= idleBreakTicks)
                break;
        }

Done:
        if (benchStartTicks <= 0 || recvCount <= 0)
            return GatewayServerResult.Empty;

        double elapsedSeconds = (benchEndTicks - benchStartTicks)
            / (double)Stopwatch.Frequency;
        double configuredSeconds = Math.Max(1.0, config.DurationSeconds);
        double throughput = recvCount / configuredSeconds;
        var latency = ComputeLatencyStats(latencySamples);
        double fallbackLatencyUs = (configuredSeconds * 1_000_000.0)
            / Math.Max(1.0, recvCount);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
        return new GatewayServerResult(true, throughput, latencyUs, latencyP95Us,
            latencyP99Us);
    }

    private static Gateway.PreparedService[] BuildClientServices(Gateway gateway,
        int clientCount)
    {
        var services = new Gateway.PreparedService[Math.Max(0, clientCount)];
        for (int i = 0; i < services.Length; i++)
            services[i] = gateway.PrepareService($"c{i}");
        return services;
    }

    private static bool TryParseClientIndex(ReadOnlySpan<byte> routingId,
        int maxExclusive, out int clientIndex)
    {
        clientIndex = -1;
        if (routingId.Length < 2 || routingId[0] != (byte)'c')
            return false;

        int value = 0;
        for (int i = 1; i < routingId.Length; i++)
        {
            byte b = routingId[i];
            if (b < (byte)'0' || b > (byte)'9')
                return false;
            value = (value * 10) + (b - (byte)'0');
            if (value < 0)
                return false;
        }

        if (value < 0 || value >= maxExclusive)
            return false;

        clientIndex = value;
        return true;
    }

    private static void ApplyRegistrySocketOptions(Registry registry,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        registry.SetOption(RegistrySocketRole.Pub, SocketOptions.Linger, 0);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.Linger, 0);
        registry.SetOption(RegistrySocketRole.PeerSub, SocketOptions.Linger, 0);
        registry.SetOption(RegistrySocketRole.Pub, SocketOptions.SndHwm, sndHwm);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.SndHwm, sndHwm);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.RcvHwm, rcvHwm);
        registry.SetOption(RegistrySocketRole.PeerSub, SocketOptions.RcvHwm, rcvHwm);
        registry.SetOption(RegistrySocketRole.Pub, SocketOptions.SndTimeo,
            sndTimeoutMs);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.SndTimeo,
            sndTimeoutMs);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
        registry.SetOption(RegistrySocketRole.PeerSub, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
    }

    private static void ApplyReceiverSocketOptions(Receiver receiver,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        receiver.SetOption(SocketOptions.Linger, 0);
        receiver.SetOption(SocketOptions.SndHwm, sndHwm);
        receiver.SetOption(SocketOptions.RcvHwm, rcvHwm);
        receiver.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        receiver.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
    }

    private static bool TryReceiveGatewayRequest(Receiver receiver,
        byte[] routingId, byte[] payload, out int routingIdLength,
        out int payloadLength)
    {
        try
        {
            payloadLength = receiver.ReceiveSinglePayload(payload.AsSpan(),
                routingId.AsSpan(), out routingIdLength,
                ReceiveFlags.DontWait);
            return payloadLength > 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            routingIdLength = 0;
            payloadLength = 0;
            return false;
        }
    }

    private static void EnqueueOrSendGatewayReply(Gateway gateway,
        PendingGatewayReply[] pendingReplies, ref int pendingReplyCount,
        Gateway.PreparedService service,
        ReadOnlySpan<byte> payload)
    {
        if (!TrySendGatewayEcho(gateway, service, payload))
            EnqueuePendingReply(pendingReplies, ref pendingReplyCount,
                service, payload);
    }

    private static void FlushPendingReplies(Gateway gateway,
        PendingGatewayReply[] pendingReplies, ref int pendingReplyCount)
    {
        for (int i = 0; i < pendingReplyCount;)
        {
            PendingGatewayReply reply = pendingReplies[i];
            if (reply.Service == null
                || !TryGatewayReady(gateway, reply.Service)
                || !TrySendGatewayEcho(gateway, reply.Service,
                    reply.Payload.AsSpan(0, reply.PayloadLength)))
            {
                i++;
                continue;
            }

            pendingReplyCount--;
            if (i != pendingReplyCount)
                pendingReplies[i].CopyFrom(pendingReplies[pendingReplyCount]);
            pendingReplies[pendingReplyCount].Clear();
        }
    }

    private static PendingGatewayReply[] CreatePendingReplies(int count,
        int payloadCapacity)
    {
        int capacity = Math.Max(1, count);
        var replies = new PendingGatewayReply[capacity];
        for (int i = 0; i < replies.Length; i++)
            replies[i] = new PendingGatewayReply(payloadCapacity);
        return replies;
    }

    private static void ConnectRegistryWithRetry(Action connect)
    {
        Exception? last = null;
        if (WaitUntil(() =>
            {
                try
                {
                    connect();
                    return true;
                }
                catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                                || IsInterrupted(ex.Errno))
                {
                    last = ex;
                    return false;
                }
            }, 5000, 20))
        {
            return;
        }

        if (last != null)
            throw last;
        throw new TimeoutException("registry connect timeout");
    }

    private static void EnqueuePendingReply(PendingGatewayReply[] pendingReplies,
        ref int pendingReplyCount, Gateway.PreparedService service,
        ReadOnlySpan<byte> payload)
    {
        if (pendingReplyCount >= pendingReplies.Length)
            throw new InvalidOperationException("gateway_pending_reply_overflow");

        pendingReplies[pendingReplyCount].Set(service, payload);
        pendingReplyCount++;
    }

    private static bool TryGatewayReady(Gateway gateway,
        Gateway.PreparedService service)
    {
        if (service == null)
            throw new InvalidOperationException("gateway_empty_service_name:ready");

        try
        {
            return gateway.ConnectionCount(service.Name) > 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno)
                                        || IsTransientNetworkError(ex.Errno))
        {
            return false;
        }
    }

    private static bool TrySendGatewayEcho(Gateway gateway,
        Gateway.PreparedService service,
        ReadOnlySpan<byte> payload)
    {
        if (service == null)
            throw new InvalidOperationException("gateway_empty_service_name:send");

        try
        {
            gateway.Send(service, payload, SendFlags.DontWait);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno)
                                        || IsTransientNetworkError(ex.Errno))
        {
            return false;
        }
    }

    private readonly struct GatewayServerConfig
    {
        internal GatewayServerConfig(string transport, int size, int warmupSeconds,
            int durationSeconds, int settleMs, int sndTimeoutMs, int rcvTimeoutMs,
            int readyTimeoutMs, int clientCount, int latencySampleCap,
            string endpoint, string registryPub, string registryRouter)
        {
            Transport = transport;
            Size = size;
            WarmupSeconds = warmupSeconds;
            DurationSeconds = durationSeconds;
            SettleMs = settleMs;
            SndTimeoutMs = sndTimeoutMs;
            RcvTimeoutMs = rcvTimeoutMs;
            ReadyTimeoutMs = readyTimeoutMs;
            ClientCount = clientCount;
            LatencySampleCap = latencySampleCap;
            Endpoint = endpoint;
            RegistryPub = registryPub;
            RegistryRouter = registryRouter;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int WarmupSeconds { get; }
        internal int DurationSeconds { get; }
        internal int SettleMs { get; }
        internal int SndTimeoutMs { get; }
        internal int RcvTimeoutMs { get; }
        internal int ReadyTimeoutMs { get; }
        internal int ClientCount { get; }
        internal int LatencySampleCap { get; }
        internal string Endpoint { get; }
        internal string RegistryPub { get; }
        internal string RegistryRouter { get; }
    }

    private readonly struct GatewayServerResult
    {
        internal static readonly GatewayServerResult Empty = new(
            false, 0.0, 0.0, 0.0, 0.0);

        internal GatewayServerResult(bool hasValue, double throughput,
            double latencyUs, double latencyP95Us, double latencyP99Us)
        {
            HasValue = hasValue;
            Throughput = throughput;
            LatencyUs = latencyUs;
            LatencyP95Us = latencyP95Us;
            LatencyP99Us = latencyP99Us;
        }

        internal bool HasValue { get; }
        internal double Throughput { get; }
        internal double LatencyUs { get; }
        internal double LatencyP95Us { get; }
        internal double LatencyP99Us { get; }
    }

    private sealed class PendingGatewayReply
    {
        internal PendingGatewayReply(int payloadCapacity)
        {
            Payload = new byte[Math.Max(1, payloadCapacity)];
        }

        internal Gateway.PreparedService? Service { get; private set; }
        internal byte[] Payload { get; }
        internal int PayloadLength { get; private set; }

        internal void Set(Gateway.PreparedService service,
            ReadOnlySpan<byte> payload)
        {
            Service = service;
            payload.CopyTo(Payload);
            PayloadLength = payload.Length;
        }

        internal void Clear()
        {
            Service = null;
            PayloadLength = 0;
        }

        internal void CopyFrom(PendingGatewayReply other)
        {
            Service = other.Service;
            other.Payload.AsSpan(0, other.PayloadLength).CopyTo(Payload);
            PayloadLength = other.PayloadLength;
        }
    }
}
