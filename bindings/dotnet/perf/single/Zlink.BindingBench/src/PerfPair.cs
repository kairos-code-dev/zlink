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
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int latCount = ResolveSingleLatencyCount("PAIR");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new PairSocket(ctx);
        using var right = new PairSocket(ctx);
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

    private static bool RunPhase(SocketBase sender, SocketBase receiver,
        byte[] payload, int payloadSize, int warmupCount, int durationSeconds,
        int recvTimeoutMs, int latencyCap, out long receivedOut,
        out List<double> latencySamples)
    {
        bool active = durationSeconds > 0;
        long deadlineTicks = active
            ? DeadlineTicksFromSeconds(durationSeconds)
            : 0;
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
                ReservoirSample(samples, latencyUs, ref sampleSeen,
                    latencyCap, ref rng);
            }

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    if (!receiver.TryReceive(recvBuffer.AsSpan(),
                            done ? ReceiveFlags.DontWait : ReceiveFlags.None,
                            out int bytesRead))
                    {
                        if (done && Stopwatch.GetTimestamp() - lastRecvTicks
                            >= recvFlushTicks)
                        {
                            break;
                        }
                        Thread.Yield();
                        continue;
                    }

                    lastRecvTicks = Stopwatch.GetTimestamp();
                    AccountMessage(bytesRead);

                    while (receiver.TryReceive(recvBuffer.AsSpan(),
                        ReceiveFlags.DontWait, out bytesRead))
                    {
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(bytesRead);
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
                    SendBlocking(sender, payload, SendFlags.None);
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
                    SendBlocking(sender, payload, SendFlags.None);
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
}
