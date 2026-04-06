using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using Zlink.Service;
using static PerfRunner;

internal static class PerfSpot
{
    private const string Topic = "bench";
    private const int BorrowedPublishThreshold = 65536;

    internal static int RunSpot(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("SPOT");
        if (size >= 65536 && warmupCount > 20)
            warmupCount = 20;
        int durationSeconds = PerfEnv.ReadPositive("PERF_SINGLE_DURATION_SECONDS", 5);
        int recvTimeoutMs = PerfEnv.ReadNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("SPOT");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var pubNode = new SpotNode(ctx);
        using var subNode = new SpotNode(ctx);
        using var spotPub = new Spot(pubNode);
        using var spotSub = new Spot(subNode);

        try
        {
            int sndHwm = PerfEnv.ReadPositive("PERF_SINGLE_SNDHWM",
                PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
            int rcvHwm = PerfEnv.ReadPositive("PERF_SINGLE_RCVHWM",
                PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000));
            int sndTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
            int subscriptionReadyTimeoutMs = ResolveSpotReadyTimeoutMs();

            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndHwm,
                sndHwm);
            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.SndTimeo,
                sndTimeo);
            pubNode.SetOption(SpotNodeSocketRole.Pub, SocketOptions.XPubNoDrop,
                1);
            subNode.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvHwm,
                rcvHwm);
            subNode.SetOption(SpotNodeSocketRole.Sub, SocketOptions.RcvTimeo,
                recvTimeoutMs);

            ConfigureSpotTlsPublisherIfNeeded(pubNode, transport);
            ConfigureSpotTlsSubscriberIfNeeded(subNode, transport);

            string endpoint = EndpointFor(transport, "spot");
            pubNode.Bind(endpoint);
            subNode.ConnectPeer(endpoint);
            spotSub.SetSubscription(Topic);

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!WaitForSubscriptionReady(spotPub, spotSub, payload,
                    subscriptionReadyTimeoutMs))
            {
                Console.Error.WriteLine("single_spot_error:subscription_not_ready");
                return 2;
            }

            long seq = 1;
            if (!RunPhase(spotPub, spotSub, payload, payloadSize, phase: 0,
                    ref seq, warmupCount, 0, latCount: 0,
                    out long warmupReceived, out _)
                || warmupReceived == 0)
            {
                Console.Error.WriteLine(
                    $"single_spot_error:warmup_failed:received={warmupReceived}:expected_nonzero");
                return 2;
            }

            if (!RunPhase(spotPub, spotSub, payload, payloadSize, phase: 1,
                    ref seq, 0, durationSeconds, latCount,
                    out long received, out var latencySamples))
            {
                Console.Error.WriteLine("single_spot_error:active_phase_failed");
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("SPOT", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"single_spot_error:exception:{ex.GetType().Name}:{ex.Message}");
            return 2;
        }
    }

    private static bool WaitForSubscriptionReady(Spot publisher, Spot subscriber,
        byte[] payload, int timeoutMs)
    {
        Span<byte> recv = stackalloc byte[Math.Max(payload.Length, 1)];
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (TryPublishPayload(publisher, payload) != SendResult.Sent)
                continue;

            if (TryReceiveSinglePayload(subscriber, recv, nonBlocking: true,
                    out int read)
                && read > 0)
            {
                return true;
            }

            Thread.Yield();
        }

        return false;
    }

    private static bool RunPhase(Spot sender, Spot receiver, byte[] payload,
        int payloadSize, int phase, ref long seq, int warmupCount,
        int durationSeconds, int latCount, out long receivedOut,
        out List<double> latencySamples)
    {
        bool active = durationSeconds > 0;
        long deadlineTicks = active ? DeadlineTicksFromSeconds(durationSeconds) : 0;

        long received = 0;
        int senderDone = 0;
        Exception? recvError = null;
        var samples = new List<double>(Math.Max(0, latCount));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        long recvFlushTicks = Stopwatch.Frequency / 5;

        var recvThread = new Thread(() =>
        {
            var recvBuffer = new byte[payloadSize];
            long lastRecvTicks = Stopwatch.GetTimestamp();

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
                ReservoirSample(samples, latencyUs, ref sampleSeen, latCount,
                    ref rng);
            }

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    int recvRc = TryReceiveSinglePayload(receiver, recvBuffer,
                        nonBlocking: true, out int nonBlockingRead)
                        ? nonBlockingRead : 0;

                    if (recvRc > 0)
                    {
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(recvRc);
                        continue;
                    }

                    if (done
                        && Stopwatch.GetTimestamp() - lastRecvTicks >= recvFlushTicks)
                        break;
                    Thread.Yield();
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
                if (TryPublishPayload(sender, payload) != SendResult.Sent)
                {
                    Thread.Yield();
                    continue;
                }
                seq++;
            }
        }
        else
        {
            for (int i = 0; i < warmupCount; i++)
            {
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampUs());
                while (TryPublishPayload(sender, payload) != SendResult.Sent)
                {
                    Thread.Yield();
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
            return received > 0;

        return received > 0 && (latCount == 0 || latencySamples.Count > 0);
    }

    private static SendResult TryPublishPayload(Spot publisher, byte[] payload)
    {
        if (payload.Length >= BorrowedPublishThreshold)
            return publisher.TryPublishBorrowedSingle(Topic, payload);

        return publisher.TryPublishRawSingle(Topic, payload);
    }

    private static bool TryReceiveSinglePayload(Spot subscriber, Span<byte> buffer,
        bool nonBlocking, out int read)
    {
        read = 0;
        try
        {
            int? received = subscriber.TryReceiveRawSubscribedFrame(buffer,
                nonBlocking ? 1 : 0, out byte[][] pendingFrames);
            if (received == null || pendingFrames.Length != 0)
                return false;
            read = received.Value;
            return read > 0;
        }
        catch (ZlinkException ex) when (nonBlocking && IsWouldBlock(ex.Errno))
        {
            return false;
        }
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
