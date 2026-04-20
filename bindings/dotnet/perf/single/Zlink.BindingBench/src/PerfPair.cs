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
        int latencySampleCap = ResolveSingleLatencySampleCap();

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new PairSocket(ctx);
        using var right = new PairSocket(ctx);
        ApplySingleSocketOptions(left);
        ApplySingleSocketOptions(right);
        ConfigureTlsServerIfNeeded(left, transport);
        ConfigureTlsClientIfNeeded(right, transport);
        MonitorSocket? leftMonitor = null;
        MonitorSocket? rightMonitor = null;

        try
        {
            leftMonitor = left.MonitorOpen(SocketEvent.ConnectionReady);
            rightMonitor = right.MonitorOpen(SocketEvent.ConnectionReady);

            string ep = EndpointFor(transport, "pair");
            left.Bind(ep);
            right.Connect(ep);
            if (!(WaitForConnectionReady(leftMonitor, readyTimeoutMs)
                && WaitForConnectionReady(rightMonitor, readyTimeoutMs)))
            {
                return 2;
            }

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(right, left, payload, payloadSize, size,
                    durationSeconds, recvTimeoutMs, latencySampleCap,
                    out long received,
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
            if (TryPrintUnsupportedTransportFailure("PAIR", transport, size, ex))
                return 0;
            Console.Error.WriteLine($"single_pair_error:{ex}");
            return 2;
        }
        finally
        {
            leftMonitor?.Dispose();
            rightMonitor?.Dispose();
        }
    }

    private static bool RunActivePhase(SocketBase sender, SocketBase receiver,
        byte[] payload, int payloadSize, int msgSize, int durationSeconds,
        int recvTimeoutMs, int latencyCap, out long receivedOut,
        out List<double> latencySamples)
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
            long lastRecvTicks = Stopwatch.GetTimestamp();
            var recvBuffer = new byte[payloadSize];

            void AccountMessage(int bytesRead)
            {
                if (bytesRead < PerfMetricHeaderSize)
                    return;

                if (!TryDecodeMetricHeader(recvBuffer.AsSpan(0, bytesRead),
                        out var header))
                    return;
                if (header.RunId != RunId
                    || header.Phase != ActivePhase
                    || header.MsgSize != (uint)msgSize)
                    return;

                Interlocked.Increment(ref received);
                ulong nowNs = EpochNs();
                if (nowNs < header.SentTsNs)
                    return;
                double latencyNs = nowNs - header.SentTsNs;
                ReservoirSample(samples, latencyNs, ref sampleSeen,
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
        return received > 0 && latencySamples.Count > 0;
    }

}
