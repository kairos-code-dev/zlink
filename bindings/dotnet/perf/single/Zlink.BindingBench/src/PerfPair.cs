using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPair
{
    internal static int RunPair(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("PAIR");
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int recvTimeoutMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("PAIR");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new Zlink.Socket(ctx, SocketType.Pair);
        using var right = new Zlink.Socket(ctx, SocketType.Pair);
        ApplySingleSocketOptions(left);
        ApplySingleSocketOptions(right);
        ConfigureTlsServerIfNeeded(left, transport);
        ConfigureTlsClientIfNeeded(right, transport);

        try
        {
            string ep = EndpointFor(transport, "pair");
            left.Bind(ep);
            right.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunPhase(right, left, payload, payloadSize, warmupCount, 0,
                    recvTimeoutMs, 0, out long warmupReceived, out _)
                || warmupReceived < warmupCount)
            {
                return 2;
            }

            Thread.Sleep(settleMs);

            if (!RunPhase(right, left, payload, payloadSize, 0, durationSeconds,
                    recvTimeoutMs, latCount, out long received,
                    out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("PAIR", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch
        {
            return 2;
        }
    }

    private static bool RunPhase(Zlink.Socket sender, Zlink.Socket receiver,
        byte[] payload, int payloadSize, int warmupCount, int durationSeconds,
        int recvTimeoutMs, int latencyCap, out long receivedOut,
        out List<double> latencySamples)
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

            void AccountMessage(Message message)
            {
                ReadOnlySpan<byte> frame = message.AsReadOnlySpan();
                if (frame.Length != payloadSize)
                    return;

                Interlocked.Increment(ref received);
                if (!active)
                    return;

                long nowUs = TimestampUs();
                long sentUs = DecodeHeader(frame);
                double latencyUs = Math.Max(0L, nowUs - sentUs);
                ReservoirSample(samples, latencyUs, ref sampleSeen,
                    latencyCap, ref rng);
            }

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    try
                    {
                        using Message message = receiver.ReceiveMessage(
                            done ? ReceiveFlags.DontWait : ReceiveFlags.None);
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(message);

                        while (true)
                        {
                            using Message burst = receiver.ReceiveMessage(
                                ReceiveFlags.DontWait);
                            lastRecvTicks = Stopwatch.GetTimestamp();
                            AccountMessage(burst);
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
