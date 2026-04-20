using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
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
        ConfigureTlsServerIfNeeded(left, transport);
        ConfigureTlsClientIfNeeded(right, transport);
        using var leftMonitor = left.MonitorOpen(SocketEvent.ConnectionReady);
        using var rightMonitor = right.MonitorOpen(SocketEvent.ConnectionReady);

        try
        {
            string endpoint = EndpointFor(transport, "pair");
            left.Bind(endpoint);
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

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("PAIR", transport, size, throughput, latency.mean,
                latency.p95, latency.p99);
            ctx.Shutdown();
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
            var recvBuffer = new byte[payload.Length];
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

                    while (receiver.TryRecv(out Received? receivedMessage)
                           && receivedMessage != null)
                    {
                        using (receivedMessage)
                        {
                            lastRecvTicks = Stopwatch.GetTimestamp();
                            ReadOnlySpan<byte> body = receivedMessage.FirstPart()
                                .AsReadOnlySpan();
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
                SendBlocking(sender, payload, PerfSendFlags.None);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno))
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
}
