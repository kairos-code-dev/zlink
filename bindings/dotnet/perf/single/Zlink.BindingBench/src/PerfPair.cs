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
        bool useMonitors = !string.Equals(transport, "inproc",
            StringComparison.OrdinalIgnoreCase);
        MonitorSocket? leftMonitor = null;
        MonitorSocket? rightMonitor = null;

        try
        {
            if (useMonitors)
            {
                leftMonitor = left.MonitorOpen(SocketEvent.ConnectionReady);
                rightMonitor = right.MonitorOpen(SocketEvent.ConnectionReady);
            }

            string ep = EndpointFor(transport, "pair");
            left.Bind(ep);
            right.Connect(ep);
            if (useMonitors)
            {
                if (!(WaitForConnectionReady(leftMonitor!, SingleConnectWaitMs)
                    && WaitForConnectionReady(rightMonitor!, SingleConnectWaitMs)))
                {
                    return 2;
                }
            }
            else
            {
                Thread.Sleep(SingleConnectWaitMs);
            }

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunPhase(right, left, payload, payloadSize, warmupCount, 0,
                    recvTimeoutMs, 0, out long warmupReceived, out _)
                || warmupReceived < warmupCount)
            {
                Console.Error.WriteLine(
                    $"single_pair_warmup_failed:received={warmupReceived},expected={warmupCount}");
                return 2;
            }

            if (!RunPhase(right, left, payload, payloadSize, 0, durationSeconds,
                    recvTimeoutMs, latCount, out long received,
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

                long nowNs = TimestampNs();
                long sentNs = DecodeHeader(recvBuffer.AsSpan(0, sizeof(long)));
                double latencyNs = Math.Max(0L, nowNs - sentNs);
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
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampNs());
                try
                {
                    SendBlocking(sender, payload, PerfSendFlags.None);
                }
                catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                || IsWouldBlock(ex.InternalErrno))
                {
                    Thread.Yield();
                    continue;
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
                StampHeader(payload.AsSpan(0, sizeof(long)), TimestampNs());
                try
                {
                    SendBlocking(sender, payload, PerfSendFlags.None);
                }
                catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                || IsWouldBlock(ex.InternalErrno))
                {
                    i--;
                    Thread.Yield();
                    continue;
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
        if (!active)
            return received >= warmupCount;

        return received > 0 && latencySamples.Count > 0;
    }

}
