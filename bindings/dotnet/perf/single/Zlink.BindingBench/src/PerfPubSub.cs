using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSub
{
    private const string Topic = "perf.topic";

    internal static int RunPubSub(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int latCount = ResolveSingleLatencyCount("PUBSUB");
        int readySettleMs = PerfEnv.ReadNonNegative(
            "PERF_SINGLE_PUBSUB_READY_SETTLE_MS", 1000);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var pub = new PubSocket(ctx);
        using var sub = new SubSocket(ctx);
        ApplySingleSocketOptions(pub);
        ApplySingleSocketOptions(sub);
        ConfigureTlsServerIfNeeded(pub, transport);
        ConfigureTlsClientIfNeeded(sub, transport);

        bool useMonitors = !string.Equals(transport, "inproc",
            StringComparison.OrdinalIgnoreCase);
        MonitorSocket? pubMonitor = null;
        MonitorSocket? subMonitor = null;

        try
        {
            if (useMonitors)
            {
                pubMonitor = pub.MonitorOpen(SocketEvent.ConnectionReady);
                subMonitor = sub.MonitorOpen(SocketEvent.ConnectionReady);
            }

            string ep = EndpointFor(transport, "pubsub");
            pub.SetOption(SocketOptions.XPubNoDrop, 1);
            pub.Bind(ep);
            sub.SetSubscription(Topic);
            sub.Connect(ep);

            if (useMonitors)
            {
                if (!(WaitForConnectionReady(pubMonitor!, SingleConnectWaitMs)
                    && WaitForConnectionReady(subMonitor!, SingleConnectWaitMs)))
                {
                    return 2;
                }
            }
            else
            {
                Thread.Sleep(SingleConnectWaitMs);
            }

            Thread.Sleep(Math.Max(1, readySettleMs));

            int payloadSize = Math.Max(size, sizeof(long));
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunPhase(pub, sub, payload, payloadSize, durationSeconds,
                    recvTimeoutMs, latCount, out long received,
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
        finally
        {
            pubMonitor?.Dispose();
            subMonitor?.Dispose();
        }
    }

    private static bool RunPhase(PubSocket sender, SubSocket receiver,
        byte[] payload, int payloadSize, int durationSeconds, int recvTimeoutMs,
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
            long lastRecvTicks = Stopwatch.GetTimestamp();
            using var poller = new Poller();
            var events = new PollEvent[1];
            poller.Add(receiver, PollEvents.PollIn);

            void AccountMessage(TopicMessage subscribed)
            {
                Message first = subscribed.FirstPart();
                if (first.Size != payloadSize)
                    return;

                byte[] recvBuffer = new byte[payloadSize];
                first.CopyTo(recvBuffer);
                Interlocked.Increment(ref received);

                long nowNs = TimestampNs();
                long sentNs = DecodeHeader(recvBuffer.AsSpan(0, sizeof(long)));
                double latencyNs = Math.Max(0L, nowNs - sentNs);
                ReservoirSample(samples, latencyNs, ref sampleSeen, latencyCap,
                    ref rng);
            }

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
                            try
                            {
                                using TopicMessage subscribed = receiver.Subscribe(
                                    RecvFlags.DontWait);
                                lastRecvTicks = Stopwatch.GetTimestamp();
                                AccountMessage(subscribed);
                            }
                            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                                            || IsWouldBlock(ex.InternalErrno))
                            {
                                break;
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
                sender.Publish(Topic, message);
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
