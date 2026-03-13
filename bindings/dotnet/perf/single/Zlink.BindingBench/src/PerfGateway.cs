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
        int recvTimeoutMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("GATEWAY");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        Registry? registry = null;
        Discovery? discovery = null;
        Receiver? receiver = null;
        Gateway? gateway = null;

        try
        {
            string suffix =
                $"{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}-{Guid.NewGuid()}";
            string regPub = EndpointFor("tcp", $"gw-pub-{suffix}");
            string regRouter = EndpointFor("tcp", $"gw-router-{suffix}");
            string service = "svc";

            registry = new Registry(ctx);
            registry.SetHeartbeat(5000, 60000);
            registry.SetEndpoints(regPub, regRouter);
            registry.Start();

            discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            discovery.ConnectRegistry(regRouter);
            receiver = new Receiver(ctx);
            string providerEp = EndpointFor(transport, "gateway-provider");
            ConfigureReceiverTlsServerIfNeeded(receiver, transport);
            receiver.Bind(providerEp);
            receiver.ConnectRegistry(regRouter);
            receiver.Register(service, providerEp, 1);

            gateway = new Gateway(ctx, discovery);
            ConfigureGatewayTlsClientIfNeeded(gateway, transport);
            Gateway.PreparedService preparedService =
                gateway.PrepareService(service);
            int readyTimeoutMs = ResolveGatewayReadyTimeoutMs();

            if (!WaitUntil(() => discovery.ReceiverCount(service) > 0,
                    readyTimeoutMs, 1)
                || !WaitUntil(() => gateway.ConnectionCount(service) > 0,
                    readyTimeoutMs, 1))
            {
                return 2;
            }

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunPhase(gateway, receiver, preparedService, payload, payloadSize,
                    warmupCount, 0, recvTimeoutMs, 0, out long warmupReceived,
                    out _)
                || warmupReceived < warmupCount)
            {
                return 2;
            }

            Thread.Sleep(settleMs);

            if (!RunPhase(gateway, receiver, preparedService, payload, payloadSize, 0,
                    durationSeconds, recvTimeoutMs, latCount, out long received,
                    out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("GATEWAY", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
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

    private static bool RunPhase(Gateway gateway, Receiver receiver,
        Gateway.PreparedService service, byte[] payload, int payloadSize,
        int warmupCount, int durationSeconds, int recvTimeoutMs, int latencyCap,
        out long receivedOut, out List<double> latencySamples)
    {
        bool active = durationSeconds > 0;
        long deadlineTicks = active
            ? DeadlineTicksFromSeconds(durationSeconds)
            : 0;
        long drainTicks = Math.Max(1,
            (long)Math.Ceiling(recvTimeoutMs * Stopwatch.Frequency / 1000.0));

        long received = 0;
        int senderDone = 0;
        Exception? recvError = null;
        var samples = new List<double>(Math.Max(0, latencyCap));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;

        var recvThread = new Thread(() =>
        {
            long lastRecvTicks = Stopwatch.GetTimestamp();
            var recvBuffer = new byte[payloadSize];

            void AccountMessage(int bytesRead)
            {
                if (bytesRead != payloadSize)
                    return;

                Interlocked.Increment(ref received);
                if (!active)
                    return;

                long nowUs = TimestampUs();
                long sentUs = DecodeHeader(recvBuffer.AsSpan(0, bytesRead));
                double latencyUs = Math.Max(0L, nowUs - sentUs);
                ReservoirSample(samples, latencyUs, ref sampleSeen, latencyCap,
                    ref rng);
            }

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    try
                    {
                        int bytesRead = receiver.ReceiveSinglePayload(recvBuffer,
                            done ? ReceiveFlags.DontWait : ReceiveFlags.None);
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(bytesRead);

                        while (true)
                        {
                            bytesRead = receiver.ReceiveSinglePayload(recvBuffer,
                                ReceiveFlags.DontWait);
                            lastRecvTicks = Stopwatch.GetTimestamp();
                            AccountMessage(bytesRead);
                        }
                    }
                    catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
                    {
                        continue;
                    }
                    catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
                    {
                        if (done && Stopwatch.GetTimestamp() - lastRecvTicks
                            >= drainTicks)
                        {
                            break;
                        }
                        Thread.Yield();
                    }
                }
            }
            catch (Exception ex)
            {
                recvError = ex;
            }
        });
        recvThread.IsBackground = true;
        recvThread.Start();

        bool sendFailed = false;
        if (active)
        {
            while (Stopwatch.GetTimestamp() < deadlineTicks)
            {
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampUs());
                try
                {
                    gateway.Send(service, payload.AsSpan(), SendFlags.None);
                }
                catch
                {
                    sendFailed = true;
                    break;
                }
            }
        }
        else
        {
            for (int i = 0; i < warmupCount; i++)
            {
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampUs());
                try
                {
                    gateway.Send(service, payload.AsSpan(), SendFlags.None);
                }
                catch
                {
                    sendFailed = true;
                    break;
                }
            }
        }

        Volatile.Write(ref senderDone, 1);
        recvThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendFailed || recvError != null)
            return false;

        if (!active)
            return received >= warmupCount;

        return received > 0 && latencySamples.Count > 0;
    }

    private static bool IsInterrupted(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EIntr || errno == 4;
    }

    private static bool IsWouldBlock(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EAgain || errno == 11;
    }

}
