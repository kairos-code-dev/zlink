using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
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
        ApplySingleAutoHwmMsgUnit(pub, size);
        ApplySingleAutoHwmMsgUnit(sub, size);
        RecalculateSingleAutoHwm(ctx);
        ConfigureTlsServerIfNeeded(pub, transport);
        ConfigureTlsClientIfNeeded(sub, transport);
        using var pubMonitor = pub.MonitorOpen(SocketEvent.ConnectionReady);
        using var subMonitor = sub.MonitorOpen(SocketEvent.ConnectionReady);

        try
        {
            string endpoint = EndpointFor(transport, "pubsub");
            int xpubNoDrop = PerfEnv.ReadPositive(
                "PERF_SINGLE_PUBSUB_XPUB_NODROP", 1) > 0 ? 1 : 0;
            pub.Options.NoDrop = xpubNoDrop != 0;
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
            Console.Error.WriteLine($"single_pubsub_error:{ex}");
            return 2;
        }
    }

    private static bool RunActivePhase(PubSocket sender, SubSocket receiver,
        byte[] payload, int msgSize, int durationSeconds, int recvTimeoutMs,
        int latencyCap, out long receivedOut, out List<double> latencySamples)
    {
        _ = recvTimeoutMs;
        long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);

        long received = 0;
        Exception? recvError = null;
        var samples = new List<double>(Math.Max(0, latencyCap));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;

        // PERF_SINGLE_TEST_POLICY § 1.4: receiver waits indefinitely
        // (-1 = signal-driven) and exits when the wire-level stop token
        // arrives.
        var recvThread = new Thread(() =>
        {
            using var poller = new Poller();
            var events = new PollEvent[1];
            poller.Add(receiver, PollEventFlags.PollIn);

            try
            {
                while (true)
                {
                    if (!WaitForInput(poller, events, -1))
                        continue;

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
                            if (maybe == null)
                                break;
                            Message body = maybe.FirstPart();
                            ReadOnlySpan<byte> payloadSpan = body.AsReadOnlySpan();
                            if (StopToken.IsStopToken(payloadSpan))
                                return;

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
                PerfSocketIo.Publish(sender, Topic, payload, SendFlags.None);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno))
            {
                continue;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[single-pubsub] send failed: {ex.Message}");
                sendFailed = true;
                break;
            }
        }

        // PERF_SINGLE_TEST_POLICY § 1.4: send wire-level stop token to
        // wake the subscriber out of recv.
        PublishStopTokenWithRetry(sender, Topic, "[single-pubsub]");
        recvThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendFailed || recvError != null)
            return false;

        return received > 0 && latencySamples.Count > 0;
    }

    private static bool IsInterrupted(int errno)
    {
        return PerfShared.IsInterrupted(errno);
    }
}
