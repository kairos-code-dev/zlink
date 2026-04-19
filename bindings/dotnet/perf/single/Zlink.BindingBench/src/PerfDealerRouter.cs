using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerRouter
{
    private static readonly RoutingId ClientRoutingId =
        RoutingId.FromBytes("CLIENT"u8);
    private static bool TryCleanup(DealerSocket sender, RouterSocket receiver,
        string endpoint)
    {
        bool ok = true;
        try
        {
            sender.Disconnect(endpoint);
        }
        catch
        {
            ok = false;
        }

        try
        {
            receiver.Unbind(endpoint);
        }
        catch
        {
            ok = false;
        }

        return ok;
    }

    internal static int RunDealerRouter(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int latCount = ResolveSingleLatencyCount("DEALER_ROUTER");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var receiver = new RouterSocket(ctx);
        using var sender = new DealerSocket(ctx);
        ApplySingleSocketOptions(receiver);
        ApplySingleSocketOptions(sender);
        ConfigureTlsServerIfNeeded(receiver, transport);
        ConfigureTlsClientIfNeeded(sender, transport);

        bool useMonitors = !string.Equals(transport, "inproc",
            StringComparison.OrdinalIgnoreCase);
        MonitorSocket? receiverMonitor = null;
        MonitorSocket? senderMonitor = null;
        string ep = EndpointFor(transport, "dealer-router");

        try
        {
            if (useMonitors)
            {
                receiverMonitor = receiver.MonitorOpen(SocketEvent.ConnectionReady);
                senderMonitor = sender.MonitorOpen(SocketEvent.ConnectionReady);
            }

            sender.SetRoutingId(ClientRoutingId);
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

            if (!RunPrimer(sender, receiver, Math.Max(size, sizeof(long)),
                    recvTimeoutMs))
            {
                ctx.Shutdown();
                TryCleanup(sender, receiver, ep);
                return 2;
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
            PrintResult("DEALER_ROUTER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
            ctx.Shutdown();
            TryCleanup(sender, receiver, ep);
            return 0;
        }
        catch
        {
            try
            {
                TryCleanup(sender, receiver, ep);
            }
            catch
            {
            }
            return 2;
        }
        finally
        {
            receiverMonitor?.Dispose();
            senderMonitor?.Dispose();
        }
    }

    private static bool RunPrimer(DealerSocket sender, RouterSocket receiver,
        int payloadSize, int recvTimeoutMs)
    {
        var payload = new byte[payloadSize];
        Array.Fill(payload, (byte)'p');
        StampHeader(payload.AsSpan(0, sizeof(long)), TimestampNs());
        using var poller = new Poller();
        var events = new PollEvent[1];
        poller.Add(receiver, PollEvents.PollIn);

        try
        {
            using var message = Message.FromBytes(payload);
            sender.Send(message);
        }
        catch
        {
            return false;
        }

        long deadline = DeadlineTicksFromMilliseconds(Math.Max(1000, recvTimeoutMs));
        while (Stopwatch.GetTimestamp() < deadline)
        {
            if (!WaitForInput(poller, events, 10))
                continue;
            if (!receiver.RecvNoWait(out Received? received) || received == null)
                continue;
            using (received)
            {
                if (received.Parts.Count != 1)
                    continue;
                return received.Parts[0].Size == payloadSize;
            }
        }

        return false;
    }

    private static bool RunActivePhase(DealerSocket sender, RouterSocket receiver,
        byte[] payload, int payloadSize, int durationSeconds, int recvTimeoutMs,
        int latCount, out long receivedOut, out List<double> latencySamples)
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
                            if (!receiver.RecvNoWait(out Received? receivedMessage)
                                || receivedMessage == null)
                                break;
                            using (receivedMessage)
                            {
                                if (receivedMessage.Parts.Count != 1)
                                    continue;
                                Message payloadMessage = receivedMessage.Parts[0];
                                if (payloadMessage.Size != payloadSize)
                                    continue;
                                ReadOnlySpan<byte> payload =
                                    payloadMessage.AsReadOnlySpan();
                                if (payload.Length < sizeof(long))
                                    continue;
                                long sentNs = DecodeHeader(payload[..sizeof(long)]);
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
                                            || IsWouldBlock(ex.InternalErrno)
                                            || IsTransientNetworkError(
                                                ex.InternalErrno))
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

    private static bool IsTransientNetworkError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EHostUnreach
            || code == ErrorCode.ENetUnreach
            || code == ErrorCode.ENotConn
            || code == ErrorCode.EConnRefused;
    }
}
