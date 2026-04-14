using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerRouter
{
    internal static int RunDealerRouter(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("DEALER_ROUTER");
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int latCount = ResolveSingleLatencyCount("DEALER_ROUTER");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var router = new RouterSocket(ctx);
        using var dealer = new DealerSocket(ctx);
        ApplySingleSocketOptions(router);
        ApplySingleSocketOptions(dealer);
        ConfigureTlsServerIfNeeded(router, transport);
        ConfigureTlsClientIfNeeded(dealer, transport);

        try
        {
            string ep = EndpointFor(transport, "dealer-router");
            dealer.SetRoutingId(RoutingId.FromBytes("CLIENT"u8));
            router.Bind(ep);
            dealer.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunPhase(dealer, router, payload, payloadSize, warmupCount, 0,
                    recvTimeoutMs, 0, out long warmupReceived, out _)
                || warmupReceived < warmupCount)
            {
                return 2;
            }

            if (!RunPhase(dealer, router, payload, payloadSize, 0,
                    durationSeconds, recvTimeoutMs, latCount, out long received,
                    out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("DEALER_ROUTER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
            return 0;
        }
        catch
        {
            return 2;
        }
    }

    private static bool RunPhase(SocketBase sender, SocketBase receiver,
        byte[] payload, int payloadSize, int warmupCount, int durationSeconds,
        int recvTimeoutMs, int latencyCap, out long receivedOut,
        out List<double> latencySamples)
    {
        bool active = durationSeconds > 0;
        long deadlineTicks = active ? DeadlineTicksFromSeconds(durationSeconds) : 0;
        long recvFlushTicks = Math.Max(1,
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
            var routingId = new byte[256];
            var recvBuffer = new byte[payloadSize];

            void AccountMessage(int bytesRead)
            {
                if (bytesRead != payloadSize)
                    return;

                Interlocked.Increment(ref received);
                if (!active)
                    return;

                long nowNs = TimestampNs();
                long sentNs = DecodeHeader(recvBuffer.AsSpan(0, sizeof(long)));
                double latencyNs = Math.Max(0L, nowNs - sentNs);
                ReservoirSample(samples, latencyNs, ref sampleSeen, latencyCap,
                    ref rng);
            }

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    int flags = done ? (int)ReceiveFlags.DontWait : (int)ReceiveFlags.None;
                    int recvRc = ReceiveRouterPayload(receiver, routingId, recvBuffer,
                        flags);
                    if (recvRc > 0)
                    {
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(recvRc);

                        while (true)
                        {
                            recvRc = ReceiveRouterPayload(receiver, routingId, recvBuffer,
                                (int)ReceiveFlags.DontWait);
                            if (recvRc > 0)
                            {
                                lastRecvTicks = Stopwatch.GetTimestamp();
                                AccountMessage(recvRc);
                                continue;
                            }
                            if (recvRc == 0)
                                break;
                            throw new InvalidOperationException("router_recv_failed");
                        }

                        continue;
                    }

                    if (recvRc == 0)
                    {
                        if (done && Stopwatch.GetTimestamp() - lastRecvTicks >= recvFlushTicks)
                            break;
                        Thread.Yield();
                        continue;
                    }

                    throw new InvalidOperationException("router_recv_failed");
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
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampNs());
                try
                {
                    SendBlocking(sender, payload, PerfSendFlags.None);
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
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampNs());
                try
                {
                    SendBlocking(sender, payload, PerfSendFlags.None);
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

    private static int ReceiveRouterPayload(SocketBase socket, byte[] routingId,
        byte[] payloadBuffer, int flags)
    {
        try
        {
            int? payloadLen = socket.TryReceiveRawRoutedFrame(routingId,
                payloadBuffer, flags, out _);
            return payloadLen ?? 0;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno))
        {
            return 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno))
        {
            return 0;
        }
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
