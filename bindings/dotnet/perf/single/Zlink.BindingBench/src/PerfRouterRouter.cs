using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouter
{
    internal static int RunRouterRouter(string transport, int size)
      => RunRouterRouterInternal(transport, size);

    internal static int RunRouterRouterInternal(string transport, int size)
    {
        string pattern = "ROUTER_ROUTER";
        int warmupCount = ResolveSingleWarmupCount(pattern);
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int recvTimeoutMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount(pattern);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var router1 = new Zlink.Socket(ctx, SocketType.Router);
        using var router2 = new Zlink.Socket(ctx, SocketType.Router);
        ApplySingleSocketOptions(router1);
        ApplySingleSocketOptions(router2);
        ConfigureTlsServerIfNeeded(router1, transport);
        ConfigureTlsClientIfNeeded(router2, transport);

        try
        {
            string ep = EndpointFor(transport, "router-router");
            router1.SetOption(SocketOptions.RoutingId, "ROUTER1");
            router2.SetOption(SocketOptions.RoutingId, "ROUTER2");
            router1.SetOption(SocketOptions.RouterMandatory, 1);
            router2.SetOption(SocketOptions.RouterMandatory, 1);
            router1.Bind(ep);
            router2.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            if (!PerformHandshake(router1, router2))
                return 2;

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunPhase(router1, router2, payload, payloadSize, warmupCount, 0,
                    recvTimeoutMs, latCount: 0, false, out long warmupReceived,
                    out _)
                || warmupReceived < warmupCount)
            {
                return 2;
            }

            Thread.Sleep(settleMs);

            if (!RunPhase(router1, router2, payload, payloadSize, 0,
                    durationSeconds, recvTimeoutMs, latCount, false,
                    out long received, out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult(pattern, transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch
        {
            return 2;
        }
    }

    private static bool PerformHandshake(Zlink.Socket receiver, Zlink.Socket sender)
    {
        Span<byte> routing = stackalloc byte[256];
        Span<byte> buffer = stackalloc byte[256];

        SendBlocking(sender, "ROUTER1"u8, SendFlags.SendMore);
        SendBlocking(sender, "PING"u8, SendFlags.None);
        if (ReceiveBlocking(receiver, routing, ReceiveFlags.None) != 7)
            return false;
        if (ReceiveBlocking(receiver, buffer.Slice(0, 4), ReceiveFlags.None) != 4)
            return false;

        SendBlocking(receiver, "ROUTER2"u8, SendFlags.SendMore);
        SendBlocking(receiver, "PONG"u8, SendFlags.None);
        if (ReceiveBlocking(sender, routing, ReceiveFlags.None) != 7)
            return false;
        if (ReceiveBlocking(sender, buffer.Slice(0, 4), ReceiveFlags.None) != 4)
            return false;

        return true;
    }

    private static bool RunPhase(Zlink.Socket receiver, Zlink.Socket sender,
        byte[] payload, int payloadSize, int warmupCount, int durationSeconds,
        int recvTimeoutMs, int latCount, bool usePoll, out long receivedOut,
        out List<double> latencySamples)
    {
        bool active = durationSeconds > 0;
        long deadlineTicks = active ? DeadlineTicksFromSeconds(durationSeconds) : 0;
        long drainTicks = Math.Max(1,
            (long)Math.Ceiling(recvTimeoutMs * Stopwatch.Frequency / 1000.0));

        long receivedCount = 0;
        int senderDone = 0;
        Exception? recvError = null;
        var samples = new List<double>(Math.Max(0, latCount));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;

        var recvThread = new Thread(() =>
        {
            long lastRecvTicks = Stopwatch.GetTimestamp();
            var routingId = new byte[256];
            var recvBuffer = new byte[payloadSize];
            Poller? poller = null;
            PollEvent[]? events = null;

            if (usePoll)
            {
                poller = new Poller();
                events = new PollEvent[1];
                poller.Add(receiver, PollEvents.PollIn);
            }

            void AccountMessage(int bytesRead)
            {
                if (bytesRead != payloadSize)
                    return;

                Interlocked.Increment(ref receivedCount);
                if (!active)
                    return;

                long nowUs = TimestampUs();
                long sentUs = DecodeHeader(recvBuffer.AsSpan(0, sizeof(long)));
                double latencyUs = Math.Max(0L, nowUs - sentUs);
                ReservoirSample(samples, latencyUs, ref sampleSeen, latCount,
                    ref rng);
            }

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    if (usePoll && poller != null && events != null
                        && !WaitForInput(poller, events, 0))
                    {
                        if (done && Stopwatch.GetTimestamp() - lastRecvTicks >= drainTicks)
                            break;
                        Thread.Yield();
                        continue;
                    }

                    int recvRc = ReceiveRouterPayload(receiver, routingId, recvBuffer,
                        done ? ReceiveFlags.DontWait : ReceiveFlags.None);
                    if (recvRc > 0)
                    {
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(recvRc);

                        while (true)
                        {
                            recvRc = ReceiveRouterPayload(receiver, routingId, recvBuffer,
                                ReceiveFlags.DontWait);
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
                        if (done && Stopwatch.GetTimestamp() - lastRecvTicks >= drainTicks)
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
            finally
            {
                poller?.Dispose();
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
                    SendBlocking(sender, "ROUTER1"u8, SendFlags.SendMore);
                    SendBlocking(sender, payload.AsSpan(), SendFlags.None);
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
                    SendBlocking(sender, "ROUTER1"u8, SendFlags.SendMore);
                    SendBlocking(sender, payload.AsSpan(), SendFlags.None);
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
        receivedOut = receivedCount;
        if (sendFailed || recvError != null)
            return false;

        if (!active)
            return receivedCount >= warmupCount;

        return receivedCount > 0 && latencySamples.Count > 0;
    }

    private static int ReceiveRouterPayload(Zlink.Socket socket, byte[] routingId,
        byte[] payloadBuffer, ReceiveFlags flags)
    {
        int ridLen;
        try
        {
            ridLen = socket.Receive(routingId.AsSpan(), flags);
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
        {
            return 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
        {
            return 0;
        }

        if (ridLen <= 0 || socket.GetOption(SocketOptions.RcvMore) == 0)
            return -1;

        try
        {
            int payloadLen = socket.Receive(payloadBuffer.AsSpan(), ReceiveFlags.None);
            if (payloadLen == 0 && socket.GetOption(SocketOptions.RcvMore) != 0)
                payloadLen = socket.Receive(payloadBuffer.AsSpan(), ReceiveFlags.None);
            DrainRemainingFramesNonBlocking(socket);
            return payloadLen;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
        {
            return 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
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
