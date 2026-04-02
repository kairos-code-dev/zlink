using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSub
{
    internal static int RunPubSub(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("PUBSUB");
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int recvTimeoutMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("PUBSUB");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var pub = new Zlink.Socket(ctx, SocketType.Pub);
        using var sub = new Zlink.Socket(ctx, SocketType.Sub);
        ApplySingleSocketOptions(pub);
        ApplySingleSocketOptions(sub);
        ConfigureTlsServerIfNeeded(pub, transport);
        ConfigureTlsClientIfNeeded(sub, transport);

        try
        {
            string ep = EndpointFor(transport, "pubsub");
            pub.SetOption(SocketOptions.XPubNoDrop, 1);
            sub.SetOption(SocketOptions.Subscribe, string.Empty);
            pub.Bind(ep);
            sub.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!WaitForSubscriber(pub, sub, payload))
                return 2;

            if (!RunPhase(pub, sub, payload, payloadSize, warmupCount, 0,
                    recvTimeoutMs, 0, out long warmupReceived, out _)
                || warmupReceived < warmupCount)
            {
                return 2;
            }

            Thread.Sleep(settleMs);

            if (!RunPhase(pub, sub, payload, payloadSize, 0, durationSeconds,
                    recvTimeoutMs, latCount, out long received,
                    out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("PUBSUB", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"single_pubsub_error:{ex.Message}");
            return 2;
        }
    }

    private static bool WaitForSubscriber(Zlink.Socket publisher,
        Zlink.Socket subscriber, byte[] payload)
    {
        using var poller = new Poller();
        var events = new PollEvent[1];
        poller.Add(subscriber, PollEvents.PollIn);
        using var recv = new Message();

        for (int i = 0; i < 2000; i++)
        {
            StampHeader(payload.AsSpan(0, sizeof(long)), TimestampUs());
            SendBlocking(publisher, payload, SendFlags.None);
            if (!WaitForInput(poller, events, 10))
                continue;

            try
            {
                using Message message = subscriber.ReceiveMessage(
                    ReceiveFlags.DontWait);
                if (message.Size == payload.Length)
                    return true;
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno)
                                            || IsWouldBlock(ex.Errno))
            {
            }
        }

        return false;
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
                        int bytesRead = receiver.Receive(recvBuffer.AsSpan(),
                            done ? ReceiveFlags.DontWait : ReceiveFlags.None);
                        lastRecvTicks = Stopwatch.GetTimestamp();
                        AccountMessage(bytesRead);

                        while (true)
                        {
                            bytesRead = receiver.Receive(recvBuffer.AsSpan(),
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
