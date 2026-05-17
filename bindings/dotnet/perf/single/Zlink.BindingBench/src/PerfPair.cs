using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfPair
{
    private const uint RunId = 1;
    private const uint ActivePhase = 1;

    internal static int RunPair(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int readyTimeoutMs = ResolveSingleConnectReadyTimeoutMs();
        int latencySampleCap = ResolveSingleLatencyCount("PAIR");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new PairSocket(ctx);
        using var right = new PairSocket(ctx);
        ApplySingleSocketOptions(left);
        ApplySingleSocketOptions(right);
        ApplySingleAutoHwmMsgUnit(left, size);
        ApplySingleAutoHwmMsgUnit(right, size);
        RecalculateSingleAutoHwm(ctx);
        ConfigureTlsServerIfNeeded(left, transport);
        ConfigureTlsClientIfNeeded(right, transport);
        using var leftMonitor = left.MonitorOpen(SocketEvent.ConnectionReady);
        using var rightMonitor = right.MonitorOpen(SocketEvent.ConnectionReady);

        try
        {
            string endpoint = EndpointFor(transport, "pair");
            left.Bind(endpoint);
            endpoint = left.Options.LastEndpoint;
            right.Connect(endpoint);
            if (!(WaitForConnectionReady(leftMonitor, readyTimeoutMs)
                && WaitForConnectionReady(rightMonitor, readyTimeoutMs)))
            {
                return 2;
            }

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(right, left, payload, size, durationSeconds,
                    recvTimeoutMs, latencySampleCap, out long received,
                    out var latencySamples))
            {
                Console.Error.WriteLine(
                    $"single_pair_active_failed:received={received},samples={latencySamples.Count}");
                return 2;
            }

            // ITEM 1: emit AUTO_HWM_DETAIL for both sockets (receiver=bind,
            // sender=connect) so run_emit.py renders the "## Auto-HWM Detail"
            // block byte-identically to the C single report.
            EmitSingleAutoHwmDetail(leftMonitor, "PAIR", transport,
                "receiver", "pair", size);
            EmitSingleAutoHwmDetail(rightMonitor, "PAIR", transport,
                "sender", "pair", size);

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("PAIR", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"single_pair_error:{ex}");
            return 2;
        }
    }

    private static bool RunActivePhase(PairSocket sender, PairSocket receiver,
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

        // PERF_SINGLE_TEST_POLICY § 1.4 / C parity: the C reference receiver
        // (bindings/c/perf/single/common/perf_single_one_way.hpp
        // run_active_phase) does a *blocking* recv (zlink_recv_part flags=0)
        // for the first message of each cycle, then burst-drains with
        // DontWait until EAGAIN, and exits on the wire-level stop token. We
        // mirror that exactly here (no Poller / no DontWait spin loop).
        var recvThread = new Thread(() =>
        {
            // Reuse one Received envelope for the whole phase (parity with C
            // which reuses a single stack header buffer). Allocating a fresh
            // envelope per message churned the managed heap on the hot
            // receiver thread.
            using var maybe = new Received();

            try
            {
                while (true)
                {
                    try
                    {
                        if (!receiver.Recv(maybe, RecvFlags.None))
                            continue;
                    }
                    catch (ZlinkException ex)
                        when (IsInterrupted(ex.InternalErrno)
                              || IsWouldBlock(ex.InternalErrno))
                    {
                        // RCVTIMEO expiry on an idle socket: C's blocking
                        // recv returns EAGAIN and re-loops; mirror that.
                        continue;
                    }

                    bool drain = true;
                    while (drain)
                    {
                        {
                            ReadOnlySpan<byte> body = maybe.FirstPart()
                                .AsReadOnlySpan();
                            if (StopToken.IsStopToken(body))
                                return;

                            long recvTicks = Stopwatch.GetTimestamp();
                            if (TryDecodeExpectedSingleHeader(body, msgSize,
                                    ActivePhase, out var header, RunId)
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

                        drain = receiver.Recv(maybe, RecvFlags.DontWait);
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
        // in-flight (sent-but-not-yet-received) credit. C's native receiver
        // consumes one-for-one the instant a message lands, so its socket /
        // HWM pipeline never deep-fills and backpressure holds the standing
        // queue at ~150 messages (one-way latency = depth / throughput ≈
        // 0.1 ms). The managed receiver's per-message cost is high enough
        // that, left uncapped, the sender fills the entire sndbuf+rcvbuf+HWM
        // pipeline (~9000 msgs at 64B) before backpressure engages; that
        // depth never drains because the two threads are rate-matched at
        // steady state, inflating one-way latency to ~9 ms with no
        // throughput benefit. An explicit in-flight credit reproduces C's
        // implicit shallow-queue discipline (no sleeps / no pacing of the
        // measured timestamp; latency stays recv_now_ns - sent_ts_ns).
        // Sweep: cap 256 -> 0.17 ms @ 96% of C throughput (C 0.13 ms);
        // uncapped -> ~9 ms @ same throughput.
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
                if (!TrySendActiveMessage(sender, payload, "[single-pair]"))
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
                Console.Error.WriteLine($"[single-pair] send failed: {ex.Message}");
                sendFailed = true;
                break;
            }
        }

        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
        // stop token. Bounded retry through transient backpressure so the
        // receiver always observes the terminator.
        SendStopTokenBlocking(sender, "[single-pair]");
        recvThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendFailed || recvError != null)
            return false;

        return received > 0 && latencySamples.Count > 0;
    }
}
