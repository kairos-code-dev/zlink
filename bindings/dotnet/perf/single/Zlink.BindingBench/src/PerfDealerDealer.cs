using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfDealerDealer
{
    private const uint RunId = 1;
    private const uint ActivePhase = 1;

    private static void TryCleanup(DealerSocket sender, DealerSocket receiver,
        string endpoint)
    {
        try
        {
            sender.Disconnect(endpoint);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-dealer-dealer] cleanup disconnect failed: {ex.Message}");
        }

        try
        {
            receiver.Unbind(endpoint);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-dealer-dealer] cleanup unbind failed: {ex.Message}");
        }
    }

    internal static int RunDealerDealer(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int readyTimeoutMs = ResolveSingleConnectReadyTimeoutMs();
        int latencySampleCap = ResolveSingleLatencyCount("DEALER_DEALER");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var receiver = new DealerSocket(ctx);
        using var sender = new DealerSocket(ctx);
        ApplySingleSocketOptions(receiver);
        ApplySingleSocketOptions(sender);
        ApplySingleAutoHwmMsgUnit(receiver, size);
        ApplySingleAutoHwmMsgUnit(sender, size);
        RecalculateSingleAutoHwm(ctx);
        ConfigureTlsServerIfNeeded(receiver, transport);
        ConfigureTlsClientIfNeeded(sender, transport);
        MonitorSocket? receiverMonitor = null;
        MonitorSocket? senderMonitor = null;
        string endpoint = EndpointFor(transport, "dealer-dealer");

        try
        {
            receiverMonitor = receiver.MonitorOpen(SocketEvent.ConnectionReady);
            senderMonitor = sender.MonitorOpen(SocketEvent.ConnectionReady);

            receiver.Bind(endpoint);
            endpoint = receiver.Options.LastEndpoint;
            sender.Connect(endpoint);
            if (!(WaitForConnectionReady(receiverMonitor, readyTimeoutMs)
                && WaitForConnectionReady(senderMonitor, readyTimeoutMs)))
            {
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            receiverMonitor.Dispose();
            receiverMonitor = null;
            senderMonitor.Dispose();
            senderMonitor = null;

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(sender, receiver, payload, size,
                    durationSeconds, recvTimeoutMs, latencySampleCap,
                    out long received, out var latencySamples))
            {
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("DEALER_DEALER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
            TryCleanup(sender, receiver, endpoint);
            return 0;
        }
        catch (Exception ex)
        {
            TryCleanup(sender, receiver, endpoint);
            Console.Error.WriteLine($"single_dealer_dealer_error:{ex}");
            return 2;
        }
        finally
        {
            receiverMonitor?.Dispose();
            senderMonitor?.Dispose();
        }
    }

    private static bool RunActivePhase(DealerSocket sender,
        DealerSocket receiver, byte[] payload, int msgSize,
        int durationSeconds, int recvTimeoutMs, int latencyCap,
        out long receivedOut, out List<double> latencySamples)
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
        // arrives. No `volatile bool senderDone` polling.
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

                    using var maybe = new Received();
                    while (true)
                    {
                        if (!receiver.Recv(maybe, RecvFlags.DontWait))
                            break;

                        {
                            ReadOnlySpan<byte> body = maybe.FirstPart()
                                .AsReadOnlySpan();
                            if (StopToken.IsStopToken(body))
                                return;

                            long recvTicks = Stopwatch.GetTimestamp();
                            if (!TryDecodeExpectedSingleHeader(body, msgSize,
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
                                ReservoirSample(samples, latencyNs,
                                    ref sampleSeen, latencyCap, ref rng);
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
                if (!TrySendActiveMessage(sender, payload,
                        "[single-dealer-dealer]"))
                    continue;
            }
            catch (ZlinkException ex)
                when (PerfShared.IsTransientBackpressure(ex.InternalErrno))
            {
                continue;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[single-dealer-dealer] send failed: {ex.Message}");
                sendFailed = true;
                break;
            }
        }

        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
        // stop token. Bounded retry through transient backpressure.
        SendStopTokenBlocking(sender, "[single-dealer-dealer]");
        recvThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendFailed || recvError != null)
            return false;

        return received > 0 && latencySamples.Count > 0;
    }
}
