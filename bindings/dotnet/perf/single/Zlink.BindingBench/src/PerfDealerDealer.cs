using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerDealer
{
    private static void TryCleanup(DealerSocket sender, DealerSocket receiver,
        string endpoint)
    {
        try
        {
            sender.Disconnect(endpoint);
        }
        catch
        {
        }

        try
        {
            receiver.Unbind(endpoint);
        }
        catch
        {
        }
    }

    internal static int RunDealerDealer(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int latCount = ResolveSingleLatencyCount("DEALER_DEALER");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var receiver = new DealerSocket(ctx);
        using var sender = new DealerSocket(ctx);
        ApplySingleSocketOptions(receiver);
        ApplySingleSocketOptions(sender);
        ConfigureTlsServerIfNeeded(receiver, transport);
        ConfigureTlsClientIfNeeded(sender, transport);

        bool useMonitors = !string.Equals(transport, "inproc",
            StringComparison.OrdinalIgnoreCase);
        MonitorSocket? receiverMonitor = null;
        MonitorSocket? senderMonitor = null;
        string ep = EndpointFor(transport, "dealer-dealer");

        try
        {
            if (useMonitors)
            {
                receiverMonitor = receiver.MonitorOpen(SocketEvent.ConnectionReady);
                senderMonitor = sender.MonitorOpen(SocketEvent.ConnectionReady);
            }

            receiver.Bind(ep);
            sender.Connect(ep);
            if (useMonitors)
            {
                if (!(WaitForConnectionReady(receiverMonitor!, SingleConnectWaitMs)
                    && WaitForConnectionReady(senderMonitor!, SingleConnectWaitMs)))
                {
                    ctx.Shutdown();
                    TryCleanup(sender, receiver, ep);
                    return 2;
                }
                receiverMonitor.Dispose();
                receiverMonitor = null;
                senderMonitor.Dispose();
                senderMonitor = null;
            }
            else
            {
                Thread.Sleep(SingleConnectWaitMs);
            }

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(sender, receiver, payload, payloadSize,
                    durationSeconds, recvTimeoutMs, latCount,
                    out long received, out var latencySamples))
            {
                ctx.Shutdown();
                TryCleanup(sender, receiver, ep);
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("DEALER_DEALER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
            ctx.Shutdown();
            TryCleanup(sender, receiver, ep);
            return 0;
        }
        catch
        {
            try
            {
                ctx.Shutdown();
            }
            catch
            {
            }
            TryCleanup(sender, receiver, ep);
            return 2;
        }
        finally
        {
            receiverMonitor?.Dispose();
            senderMonitor?.Dispose();
        }
    }

    private static bool RunActivePhase(DealerSocket sender,
        DealerSocket receiver, byte[] payload, int payloadSize,
        int durationSeconds, int recvTimeoutMs, int latCount,
        out long receivedOut, out List<double> latencySamples)
    {
        long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);
        long recvFlushTicks = Math.Max(1,
            (long)Math.Ceiling(recvTimeoutMs * Stopwatch.Frequency / 1000.0));

        long received = 0;
        int senderDone = 0;
        Exception? recvError = null;
        var samples = new List<double>(Math.Max(0, latCount));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;

        var recvThread = new Thread(() =>
        {
            long lastRecvTicks = Stopwatch.GetTimestamp();
            using var poller = new Poller();
            var events = new PollEvent[1];
            poller.Add(receiver, PollEvents.PollIn);

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    int timeoutMs = done ? Math.Max(1, recvTimeoutMs) : 50;
                    if (WaitForInput(poller, events, timeoutMs))
                    {
                        while (true)
                        {
                            if (!receiver.RecvNoWait(out Received? maybe)
                                || maybe is null)
                                break;

                            using (maybe)
                            {
                                Message first = maybe.FirstPart();
                                if (first.Size != payloadSize)
                                    continue;
                                byte[] recvBuffer = new byte[payloadSize];
                                first.CopyTo(recvBuffer);
                                long sentNs = DecodeHeader(
                                    recvBuffer.AsSpan(0, sizeof(long)));
                                long nowNs = TimestampNs();
                                double latencyNs = Math.Max(0L, nowNs - sentNs);
                                Interlocked.Increment(ref received);
                                ReservoirSample(samples, latencyNs,
                                    ref sampleSeen, latCount, ref rng);
                                lastRecvTicks = Stopwatch.GetTimestamp();
                            }
                        }
                        continue;
                    }

                    if (done && Stopwatch.GetTimestamp() - lastRecvTicks
                        >= recvFlushTicks)
                    {
                        break;
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
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampHeader(payload.AsSpan(0, sizeof(long)), TimestampNs());
            try
            {
                using var message = Message.FromBytes(payload);
                sender.Send(message);
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
