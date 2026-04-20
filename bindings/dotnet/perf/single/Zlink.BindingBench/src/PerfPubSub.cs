using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSub
{
    private const string Topic = "perf.topic";
    private const uint RunId = 1;
    private const uint ActivePhase = 1;

    internal static int RunPubSub(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int readyTimeoutMs = ResolveSingleConnectReadyTimeoutMs();
        int readySettleMs = ResolveSinglePubSubReadySettleMs();
        int latencySampleCap = ResolveSingleLatencyCount("PUBSUB");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var pub = new PubSocket(ctx);
        using var sub = new SubSocket(ctx);
        ApplySingleSocketOptions(pub);
        ApplySingleSocketOptions(sub);
        ConfigureTlsServerIfNeeded(pub, transport);
        ConfigureTlsClientIfNeeded(sub, transport);
        using var pubMonitor = pub.MonitorOpen(SocketEvent.ConnectionReady);
        using var subMonitor = sub.MonitorOpen(SocketEvent.ConnectionReady);

        try
        {
            string endpoint = EndpointFor(transport, "pubsub");
            pub.SetOption(SocketOptions.XPubNoDrop, 1);
            pub.Bind(endpoint);
            sub.SetSubscription(Topic);
            sub.Connect(endpoint);

            if (!(WaitForConnectionReady(pubMonitor, readyTimeoutMs)
                && WaitForConnectionReady(subMonitor, readyTimeoutMs)))
            {
                return 2;
            }

            Thread.Sleep(Math.Max(1, readySettleMs));

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(pub, sub, payload, size, durationSeconds,
                    recvTimeoutMs, latencySampleCap, out long received,
                    out var latencySamples))
            {
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("PUBSUB", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            ctx.Shutdown();
            return 0;
        }
        catch (Exception ex)
        {
            if (TryPrintUnsupportedTransportFailure("PUBSUB", transport, size, ex))
                return 0;
            Console.Error.WriteLine($"single_pubsub_error:{ex}");
            return 2;
        }
    }

    private static bool RunActivePhase(PubSocket sender, SubSocket receiver,
        byte[] payload, int msgSize, int durationSeconds, int recvTimeoutMs,
        int latencyCap, out long receivedOut, out List<double> latencySamples)
    {
        long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
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
            using var poller = new Poller();
            var events = new PollEvent[1];
            long lastRecvTicks = Stopwatch.GetTimestamp();
            poller.Add(receiver, PollEvents.PollIn);

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    int timeoutMs = done ? Math.Max(1, recvTimeoutMs) : 50;
                    if (!WaitForInput(poller, events, timeoutMs))
                    {
                        if (done && Stopwatch.GetTimestamp() - lastRecvTicks
                            >= recvFlushTicks)
                        {
                            break;
                        }

                        continue;
                    }

                    while (true)
                    {
                        TopicMessage? maybe = null;
                        try
                        {
                            maybe = receiver.Subscribe(RecvFlags.DontWait);
                        }
                        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                        || IsWouldBlock(ex.InternalErrno))
                        {
                            break;
                        }

                        using (maybe)
                        {
                            Message body = maybe.FirstPart();
                            ReadOnlySpan<byte> payloadSpan = body.AsReadOnlySpan();
                            long recvTicks = Stopwatch.GetTimestamp();
                            if (!TryDecodeExpectedSingleHeader(payloadSpan, msgSize,
                                    ActivePhase, out var header, RunId))
                            {
                                continue;
                            }

                            if (recvTicks > deadlineTicks)
                                continue;

                            Interlocked.Increment(ref received);
                            ulong nowNs = EpochNs();
                            if (nowNs >= header.SentTsNs)
                            {
                                double latencyNs = nowNs - header.SentTsNs;
                                ReservoirSample(samples, latencyNs, ref sampleSeen,
                                    latencyCap, ref rng);
                            }
                            lastRecvTicks = Stopwatch.GetTimestamp();
                        }
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
        ulong seq = 1;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), RunId, ActivePhase, msgSize, seq,
                EpochNs());
            seq++;
            try
            {
                using var message = Message.FromBytes(payload);
                sender.Publish(Topic, message);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
                continue;
            }
            catch
            {
                sendFailed = true;
                break;
            }
        }

        Volatile.Write(ref senderDone, 1);
        recvThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendFailed || recvError != null)
            return false;

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
