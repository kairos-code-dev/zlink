using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpot
{
    private const string Topic = "bench";

    internal static int RunSpot(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("SPOT");
        if (size >= 65536 && warmupCount > 20)
            warmupCount = 20;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int recvTimeoutMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("SPOT");
        int settleMs = SingleSettleTimeMs;

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        SpotNode? pubNode = null;
        SpotNode? subNode = null;
        Spot? spotPub = null;
        Spot? spotSub = null;

        try
        {
            pubNode = new SpotNode(ctx);
            subNode = new SpotNode(ctx);
            int sndHwm = ParseEnv("PERF_SINGLE_SNDHWM", ParseEnv("PERF_SINGLE_HWM", 1000));
            int rcvHwm = ParseEnv("PERF_SINGLE_RCVHWM", ParseEnv("PERF_SINGLE_HWM", 1000));
            int sndTimeo = ParseEnvNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
            int readyTimeoutMs = ResolveSpotDiscoveryTimeoutMs();
            int subscriptionReadyTimeoutMs = ResolveSpotReadyTimeoutMs();

            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndHwm, sndHwm);
            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndTimeo, sndTimeo);
            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.XPubNoDrop, 1);

            subNode.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvHwm, rcvHwm);
            subNode.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvTimeo, recvTimeoutMs);

            ConfigureSpotTlsPublisherIfNeeded(pubNode, transport);
            ConfigureSpotTlsSubscriberIfNeeded(subNode, transport);

            string endpoint = EndpointFor(transport, "spot");
            pubNode.Bind(endpoint);
            subNode.ConnectPeerPub(endpoint);

            spotPub = new Spot(pubNode);
            spotSub = new Spot(subNode);
            spotSub.Subscribe(Topic);

            if (!WaitUntil(() => subNode.GetSubPeers().Length > 0, readyTimeoutMs, 1))
                return 2;

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!WaitForSubscriptionReady(spotPub, spotSub, payload,
                    subscriptionReadyTimeoutMs))
                return 2;

            long seq = 1;
            if (!RunPhase(spotPub, spotSub, payload, payloadSize, size,
                    phase: 0, ref seq, warmupCount, 0, recvTimeoutMs, 0,
                    out long warmupReceived, out _)
                || warmupReceived < warmupCount)
            {
                return 2;
            }

            Thread.Sleep(settleMs);

            if (!RunPhase(spotPub, spotSub, payload, payloadSize, size,
                    phase: 1, ref seq, 0, durationSeconds, recvTimeoutMs, latCount,
                    out long received, out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("SPOT", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch
        {
            return 2;
        }
        finally
        {
            TryDisposeAllQuietly(spotSub, spotPub, subNode, pubNode);
        }
    }

    private static bool WaitForSubscriptionReady(Spot publisher, Spot subscriber,
        byte[] payload, int timeoutMs)
    {
        Span<byte> recv = stackalloc byte[Math.Max(payload.Length, 1)];
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            publisher.Publish(Topic, payload.AsSpan(), SendFlags.None);
            try
            {
                if (subscriber.ReceiveSinglePayload(recv, ReceiveFlags.None) > 0)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
            {
            }
        }
        return false;
    }

    private static bool RunPhase(Spot sender, Spot receiver, byte[] payload,
        int payloadSize, int msgSize, int phase, ref long seq, int warmupCount,
        int durationSeconds, int recvTimeoutMs, int latencyCap,
        out long receivedOut, out List<double> latencySamples)
    {
        bool active = durationSeconds > 0;
        long deadlineTicks = active ? DeadlineTicksFromSeconds(durationSeconds) : 0;
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
                long sentUs = DecodeHeader(recvBuffer.AsSpan(0, sizeof(long)));
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
                        int recvRc = receiver.ReceiveSinglePayload(recvBuffer,
                            done ? ReceiveFlags.DontWait : ReceiveFlags.None);
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(recvRc);

                        while (true)
                        {
                            recvRc = receiver.ReceiveSinglePayload(recvBuffer,
                                ReceiveFlags.DontWait);
                            lastRecvTicks = Stopwatch.GetTimestamp();
                            AccountMessage(recvRc);
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
                    sender.Publish(Topic, payload.AsSpan(), SendFlags.None);
                }
                catch
                {
                    sendFailed = true;
                    break;
                }
                seq++;
            }
        }
        else
        {
            for (int i = 0; i < warmupCount; i++)
            {
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampUs());
                try
                {
                    sender.Publish(Topic, payload.AsSpan(), SendFlags.None);
                }
                catch
                {
                    sendFailed = true;
                    break;
                }
                seq++;
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

    private static bool IsWouldBlock(int errno)
    {
        return ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain;
    }

    private static bool IsInterrupted(int errno)
    {
        return ZlinkException.MapErrorCode(errno) == ErrorCode.EIntr;
    }
}
