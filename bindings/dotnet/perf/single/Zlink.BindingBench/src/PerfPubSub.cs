using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfPubSub
{
    // PERF parity: C single PUBSUB publishes on topic "bench", subscribes
    // to the empty prefix (receive-all), and filters received messages by
    // topic. Matches bindings/c/perf/single/src/perf_pubsub.cpp.
    private const string Topic = "bench";
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
        ApplySingleAutoHwmMsgUnit(ctx, size);
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
            endpoint = pub.Options.LastEndpoint;
            sub.SetSubscription(string.Empty);
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

            // ITEM 1: byte-identical AUTO_HWM_DETAIL emission (C parity).
            EmitSingleAutoHwmDetail(pubMonitor, "PUBSUB", transport,
                "publisher", "pub", size);
            EmitSingleAutoHwmDetail(subMonitor, "PUBSUB", transport,
                "subscriber", "sub", size);

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("PUBSUB", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
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

        // PERF_SINGLE_TEST_POLICY § 1.4 / C parity: blocking first subscribe
        // per cycle (zlink_recv flags=0), then DontWait burst-drain, exit on
        // the wire-level stop token. Mirrors C perf_pubsub.cpp (no Poller /
        // no DontWait spin loop).
        var recvThread = new Thread(() =>
        {
            // Reuse one TopicMessage envelope for the whole phase (parity
            // with C which reuses a single stack header buffer).
            using var maybe = new TopicMessage();

            try
            {
                while (true)
                {
                    try
                    {
                        if (!receiver.Subscribe(maybe, RecvFlags.None))
                            continue;
                    }
                    catch (ZlinkException ex)
                        when (IsInterrupted(ex.InternalErrno)
                              || IsWouldBlock(ex.InternalErrno))
                    {
                        continue;
                    }

                    bool drain = true;
                    while (drain)
                    {
                        if (string.Equals(maybe.Topic, Topic,
                                StringComparison.Ordinal))
                        {
                            Message body = maybe.FirstPart();
                            ReadOnlySpan<byte> payloadSpan =
                                body.AsReadOnlySpan();
                            if (StopToken.IsStopToken(payloadSpan))
                                return;

                            long recvTicks = Stopwatch.GetTimestamp();
                            if (TryDecodeExpectedSingleHeader(payloadSpan,
                                    msgSize, ActivePhase, out var header, RunId)
                                && recvTicks <= deadlineTicks)
                            {
                                Interlocked.Increment(ref received);
                                ulong nowNs = EpochNs();
                                if (nowNs >= header.SentTsNs)
                                {
                                    double latencyNs = nowNs - header.SentTsNs;
                                    ReservoirSample(samples, latencyNs,
                                        ref sampleSeen, latencyCap, ref rng);
                                }
                            }
                        }

                        try
                        {
                            drain = receiver.Subscribe(maybe,
                                RecvFlags.DontWait);
                        }
                        catch (ZlinkException ex)
                            when (IsInterrupted(ex.InternalErrno)
                                  || IsWouldBlock(ex.InternalErrno))
                        {
                            drain = false;
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

        // PERF_SINGLE_TEST_POLICY § 1.4 / C parity: bound the sender's
        // in-flight (published-but-not-received) credit so the standing
        // queue depth matches C's (~256). C's native subscriber consumes
        // one-for-one so its pipeline never deep-fills; uncapped, the
        // managed publisher fills the entire sndbuf+rcvbuf+HWM pipeline
        // before backpressure engages and that depth never drains at
        // steady-state rate-match, inflating one-way latency with no
        // throughput gain. The credit reproduces C's implicit shallow-queue
        // discipline without sleeps/pacing; latency stays
        // recv_now_ns - sent_ts_ns.
        long inflightCap = PerfEnv.ReadNonNegative(
            "PERF_SINGLE_INFLIGHT_CAP", 256);

        bool sendFailed = false;
        ulong seq = 1;
        long sent = 0;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (inflightCap > 0)
            {
                while (sent - Interlocked.Read(ref received) >= inflightCap
                       && Stopwatch.GetTimestamp() < deadlineTicks)
                {
                    Thread.SpinWait(1);
                }
            }
            StampMetricHeader(payload.AsSpan(), RunId, ActivePhase, msgSize, seq,
                EpochNs());
            seq++;
            try
            {
                if (!TryPublishActiveMessage(sender, Topic, payload,
                        "[single-pubsub]"))
                    continue;
                sent++;
            }
            catch (ZlinkException ex)
                when (PerfShared.IsTransientBackpressure(ex.InternalErrno))
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

        PublishStopTokenBlocking(sender, Topic, "[single-pubsub]");
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
