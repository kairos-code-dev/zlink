using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfGateway
{
    internal static int RunGateway(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("GATEWAY");
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("GATEWAY");

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();
        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        Registry? registry = null;
        Discovery? discovery = null;
        Receiver? receiver = null;
        Gateway? gateway = null;

        try
        {
            string suffix = $"{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}-{Guid.NewGuid()}";
            string regPub = EndpointFor("tcp", $"gw-pub-{suffix}");
            string regRouter = EndpointFor("tcp", $"gw-router-{suffix}");

            registry = new Registry(ctx);
            registry.SetHeartbeat(5000, 60000);
            registry.SetEndpoints(regPub, regRouter);
            registry.Start();

            discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            ConnectRegistryWithRetry(() => discovery.ConnectRegistry(regRouter));
            string service = "svc";
            discovery.Subscribe(service);

            receiver = new Receiver(ctx);
            string providerEp = EndpointFor(transport, "gateway-provider");
            ConfigureReceiverTlsServerIfNeeded(receiver, transport);
            receiver.Bind(providerEp);
            ConnectRegistryWithRetry(() => receiver.ConnectRegistry(regRouter));
            receiver.Register(service, providerEp, 1);
            receiver.SetOption(SocketOptions.RcvTimeo, 5000);

            gateway = new Gateway(ctx, discovery);
            ConfigureGatewayTlsClientIfNeeded(gateway, transport);
            if (!WaitUntil(() => discovery.ReceiverCount(service) > 0, 5000))
                return 2;
            if (!WaitUntil(() => gateway.ConnectionCount(service) > 0, 5000))
                return 2;
            if (!WaitUntil(() =>
                {
                    try
                    {
                        var receivers = discovery.GetReceivers(service);
                        if (receivers.Length == 0)
                            return false;
                        return !string.IsNullOrWhiteSpace(
                            receivers[0].RoutingId);
                    }
                    catch
                    {
                        return false;
                    }
                }, 5000))
                return 2;
            Thread.Sleep(SingleConnectWaitMs);

            var payload = new byte[size];
            Array.Fill(payload, (byte)'a');
            var data = new byte[Math.Max(256, size)];

            for (int i = 0; i < warmupCount; i++)
            {
                SendGatewayWithRetry(gateway, service, payload.AsSpan(), 5000);
                _ = ReceiveProviderPayloadWithRetry(receiver, data.AsSpan(), 5000);
            }

            Thread.Sleep(settleMs);

            int recvCount = 0;
            long benchStartTicks = Stopwatch.GetTimestamp();
            long throughputDeadlineTicks = DeadlineTicksFromSeconds(
                durationSeconds);
            while (Stopwatch.GetTimestamp() < throughputDeadlineTicks)
            {
                SendGatewayWithRetry(gateway, service, payload.AsSpan(), 5000);
                _ = ReceiveProviderPayloadWithRetry(receiver, data.AsSpan(), 5000);
                recvCount++;
            }

            double elapsedSeconds = ElapsedSecondsFromTicks(benchStartTicks,
                Stopwatch.GetTimestamp());
            double thr = recvCount > 0 && elapsedSeconds > 0.0
                ? recvCount / elapsedSeconds
                : 0.0;

            if (drainMs > 0)
                Thread.Sleep(drainMs);

            var latSamples = new List<double>(latCount);
            long sampleSeen = 0;
            uint rng = 0xA341316Cu;
            for (int i = 0; i < latCount; i++)
            {
                long begin = Stopwatch.GetTimestamp();
                SendGatewayWithRetry(gateway, service, payload.AsSpan(), 5000);
                _ = ReceiveProviderPayloadWithRetry(receiver, data.AsSpan(), 5000);
                long end = Stopwatch.GetTimestamp();
                double latUs = (end - begin) * 1_000_000.0 / Stopwatch.Frequency;
                ReservoirSample(latSamples, latUs, ref sampleSeen, latCount,
                    ref rng);
            }
            var latency = ComputeLatencyStats(latSamples);
            PrintResult("GATEWAY", transport, size, thr, latency.mean,
                latency.p95, latency.p99);
            wall.Stop();
            PrintSingleProcessMetrics("GATEWAY", transport, size, cpuStart, wall);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"single_gateway_error:{ex.Message}");
            return 2;
        }
        finally
        {
            TryDisposeAllQuietly(gateway, receiver, discovery, registry);
        }
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
                catch (ZlinkException ex) when (
                    ZlinkException.MapErrorCode(ex.Errno) == ErrorCode.EAgain
                    || ZlinkException.MapErrorCode(ex.Errno) == ErrorCode.EIntr)
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

    private static void SendGatewayWithRetry(Gateway gateway, string serviceName,
        ReadOnlySpan<byte> payload, int timeoutMs)
    {
        Exception? last = null;
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                gateway.Send(serviceName, payload, SendFlags.DontWait);
                return;
            }
            catch (ZlinkException ex) when (
                ZlinkException.MapErrorCode(ex.Errno) == ErrorCode.EAgain
                || ZlinkException.MapErrorCode(ex.Errno) == ErrorCode.EIntr)
            {
                last = ex;
            }
            Thread.Sleep(1);
        }

        if (last != null)
            throw last;
        throw new TimeoutException("gateway send timeout");
    }

    private static int ReceiveProviderPayloadWithRetry(Receiver receiver,
        Span<byte> payloadBuffer, int timeoutMs)
    {
        Exception? last = null;
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                int received = receiver.ReceiveSinglePayload(payloadBuffer,
                    ReceiveFlags.DontWait);
                if (received > 0)
                    return received;
            }
            catch (ZlinkException ex) when (
                ZlinkException.MapErrorCode(ex.Errno) == ErrorCode.EAgain
                || ZlinkException.MapErrorCode(ex.Errno) == ErrorCode.EIntr)
            {
                last = ex;
            }
            Thread.Sleep(1);
        }

        if (last != null)
            throw last;
        throw new TimeoutException("gateway receive timeout");
    }
}
