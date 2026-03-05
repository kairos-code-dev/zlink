using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Zlink;
using Zlink.Service;
using static PerfRunner;

internal static class PerfGatewayServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "GATEWAY";
        const string serviceName = "perf-gateway";
        const string serverGatewayRoutingId = "sg";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int sndTimeoutMs = ResolveMultiSndTimeoutMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int clientCount = ResolveMultiClients(pattern);
        int latencySampleCap = ResolveMultiLatencySampleCap();
        string endpoint = MultiEndpointFor(transport, "multi-gateway");
        string registryPub = EndpointFor("tcp", "multi-gateway-reg-pub");
        string registryRouter = EndpointFor("tcp", "multi-gateway-reg-router");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        Registry? registry = null;
        Receiver? receiver = null;
        Discovery? discovery = null;
        Gateway? gateway = null;
        Zlink.Socket? receiverRouter = null;
        Zlink.Socket? gatewayRouter = null;
        try
        {
            registry = new Registry(ctx);
            registry.SetHeartbeat(5000, 60000);
            registry.SetEndpoints(registryPub, registryRouter);
            ApplyRegistrySocketOptions(registry, pattern, sndTimeoutMs,
                rcvTimeoutMs);
            registry.Start();

            receiver = new Receiver(ctx);
            ConfigureReceiverTlsServerIfNeeded(receiver, transport);
            receiver.Bind(endpoint);
            receiver.ConnectRegistry(registryRouter);
            receiver.Register(serviceName, endpoint, 1);
            ApplyReceiverSocketOptions(receiver, pattern, sndTimeoutMs,
                rcvTimeoutMs);
            receiverRouter = receiver.CreateRouterSocket();
            ApplyMultiSocketOptions(receiverRouter, pattern);
            receiverRouter.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);

            discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            discovery.ConnectRegistry(registryPub);
            ApplyDiscoverySocketOptions(discovery, pattern, sndTimeoutMs,
                rcvTimeoutMs);
            for (int i = 0; i < clientCount; i++)
                discovery.Subscribe($"c{i}");
            gateway = new Gateway(ctx, discovery, serverGatewayRoutingId);
            ConfigureGatewayTlsClientIfNeeded(gateway, transport);
            gateway.SetOption(SocketOptions.SndHwm,
                ResolveMultiHwmValue("PERF_SNDHWM", pattern));
            gateway.SetOption(SocketOptions.RcvHwm,
                ResolveMultiHwmValue("PERF_RCVHWM", pattern));
            gateway.SetOption(SocketOptions.Linger, 0);
            gateway.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
            gateway.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
            gatewayRouter = gateway.CreateRouterSocket();
            ApplyMultiSocketOptions(gatewayRouter, pattern);

            Console.WriteLine($"READY,{endpoint}|{registryPub}|{registryRouter}");
            _ = WaitUntil(() => receiver.GetRouterPeers().Length > 0,
                readyTimeoutMs);

            var routingId = new byte[256];
            var payload = new byte[Math.Max(256, Math.Max(Math.Max(size,
                PerfMetricHeaderSize), MultiStopToken.Length))];
            long recvCount = 0;
            long benchStartTicks = 0;
            long benchEndTicks = 0;
            long activeHardStopTicks = 0;
            int settleSeconds = (settleMs + 999) / 1000;
            long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
                + (long)Math.Max(6, warmupSeconds + settleSeconds + 3)
                * Stopwatch.Frequency;
            long lastActiveMessageTicks = 0;
            long idleBreakTicks = (long)(Stopwatch.Frequency
                * (Math.Max(rcvTimeoutMs * 2, 1000) / 1000.0));
            var latSamples = new List<double>(latencySampleCap);
            long sampleSeen = 0;
            uint rng = 0xA341316Cu;
            const uint expectedRunId = 1;

            while (true)
            {
                if (activeHardStopTicks > 0
                    && Stopwatch.GetTimestamp() >= activeHardStopTicks)
                    break;

                int ridLen = ReceiveRetry(receiverRouter, routingId.AsSpan(),
                    ReceiveFlags.DontWait);
                if (ridLen <= 0)
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
                    Thread.Yield();
                    continue;
                }

                int n = ReceiveRetry(receiverRouter, payload.AsSpan(),
                    ReceiveFlags.DontWait);
                if (n <= 0)
                {
                    long nowTicks = Stopwatch.GetTimestamp();
                    if (activeHardStopTicks > 0 && nowTicks >= activeHardStopTicks)
                        break;
                    if (lastActiveMessageTicks > 0
                        && nowTicks - lastActiveMessageTicks >= idleBreakTicks)
                        break;
                    Thread.Yield();
                    continue;
                }

                ReadOnlySpan<byte> body = payload.AsSpan(0, n);
                if (IsStopTokenPayload(body))
                    break;

                string clientService = Encoding.UTF8.GetString(routingId, 0, ridLen);
                _ = TrySendGatewayEcho(gateway, clientService, body);
                if (!TryDecodeMetricHeader(body, out PerfMetricHeader header))
                    continue;
                if (header.RunId != expectedRunId
                    || header.MsgSize != (uint)size
                    || header.Phase != (uint)PerfPhase.Active)
                    continue;

                if (benchStartTicks == 0)
                {
                    benchStartTicks = Stopwatch.GetTimestamp();
                    activeHardStopTicks = benchStartTicks
                        + (long)Math.Max(2, durationSeconds + 1)
                        * Stopwatch.Frequency;
                }
                benchEndTicks = Stopwatch.GetTimestamp();
                lastActiveMessageTicks = benchEndTicks;
                recvCount++;
                ulong nowUs = EpochUs();
                if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                {
                    double sampleUs = nowUs - header.SentTsUs;
                    ReservoirSample(latSamples, sampleUs, ref sampleSeen,
                        latencySampleCap, ref rng);
                }
            }

            if (benchStartTicks > 0 && recvCount > 0)
            {
                double elapsedSeconds = (benchEndTicks - benchStartTicks)
                    / (double)Stopwatch.Frequency;
                double throughput = elapsedSeconds > 0.0
                    ? recvCount / elapsedSeconds
                    : 0.0;
                var latency = ComputeLatencyStats(latSamples);
                double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
                    / Math.Max(1.0, recvCount);
                double latencyUs = latency.mean > 0.0 ? latency.mean
                    : fallbackLatencyUs;
                double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
                double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
                PrintResult(pattern, transport, size, throughput, latencyUs,
                    latencyP95Us, latencyP99Us);
            }

            return 0;
        }
        finally
        {
            TryDisposeQuietly(gatewayRouter);
            TryDisposeQuietly(gateway);
            TryDisposeQuietly(discovery);
            TryDisposeQuietly(receiverRouter);
            TryDisposeQuietly(receiver);
            TryDisposeQuietly(registry);
        }
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
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.Linger, 0);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.Linger, 0);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.SndHwm, sndHwm);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.RcvHwm, rcvHwm);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.SndHwm, sndHwm);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.RcvHwm, rcvHwm);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.SndTimeo,
            sndTimeoutMs);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.SndTimeo,
            sndTimeoutMs);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
    }

    private static void ApplyDiscoverySocketOptions(Discovery discovery,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.Linger, 0);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.SndHwm, sndHwm);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.RcvHwm, rcvHwm);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.SndTimeo,
            sndTimeoutMs);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
    }

    private static bool TrySendGatewayEcho(Gateway gateway, string serviceName,
        ReadOnlySpan<byte> payload)
    {
        try
        {
            gateway.Send(serviceName, payload, SendFlags.DontWait);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno)
                                        || IsTransientNetworkError(ex.Errno))
        {
            return false;
        }
    }
}
