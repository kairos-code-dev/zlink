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
    private const uint RunId = 1;
    private const uint ReadyPhase = 0;
    private const uint ActivePhase = 1;

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
        int readyTimeoutMs = ResolveSingleConnectReadyTimeoutMs();
        int latencySampleCap = ResolveSingleLatencyCount("DEALER_ROUTER");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var receiver = new RouterSocket(ctx);
        using var sender = new DealerSocket(ctx);
        ApplySingleSocketOptions(receiver);
        ApplySingleSocketOptions(sender);
        ConfigureTlsServerIfNeeded(receiver, transport);
        ConfigureTlsClientIfNeeded(sender, transport);
        MonitorSocket? receiverMonitor = null;
        MonitorSocket? senderMonitor = null;
        string endpoint = EndpointFor(transport, "dealer-router");

        try
        {
            receiverMonitor = receiver.MonitorOpen(SocketEvent.ConnectionReady);
            senderMonitor = sender.MonitorOpen(SocketEvent.ConnectionReady);

            sender.SetRoutingId(ClientRoutingId);
            receiver.Bind(endpoint);
            sender.Connect(endpoint);
            if (!(WaitForConnectionReady(receiverMonitor, readyTimeoutMs)
                && WaitForConnectionReady(senderMonitor, readyTimeoutMs)))
            {
                DebugLog("single_dealer_router_error:connection_not_ready");
                ctx.Shutdown();
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            if (!VerifyRouteReady(sender, receiver, size, recvTimeoutMs))
            {
                ctx.Shutdown();
                TryCleanup(sender, receiver, endpoint);
                DebugLog("single_dealer_router_error:route_probe_failed");
                return 2;
            }

            receiverMonitor.Dispose();
            receiverMonitor = null;
            senderMonitor.Dispose();
            senderMonitor = null;

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(sender, receiver, payload, size, durationSeconds,
                    recvTimeoutMs, latencySampleCap, out long received,
                    out var latencySamples))
            {
                DebugLog("single_dealer_router_error:active_failed");
                ctx.Shutdown();
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("DEALER_ROUTER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
            ctx.Shutdown();
            TryCleanup(sender, receiver, endpoint);
            return 0;
        }
        catch (Exception ex)
        {
            TryCleanup(sender, receiver, endpoint);
            DebugLog($"single_dealer_router_error:exception:{ex}");
            return 2;
        }
        finally
        {
            receiverMonitor?.Dispose();
            senderMonitor?.Dispose();
        }
    }

    private static bool VerifyRouteReady(DealerSocket sender,
        RouterSocket receiver, int msgSize, int recvTimeoutMs)
    {
        int payloadSize = Math.Max(msgSize, PerfMetricHeaderSize);
        var probe = new byte[payloadSize];
        Array.Fill(probe, (byte)'p');
        StampMetricHeader(probe.AsSpan(), RunId, ReadyPhase, msgSize, 1, EpochNs());

        try
        {
            PerfSocketIo.Send(sender, probe, SendFlags.None);
        }
        catch (ZlinkRecvException ex)
        {
            return false;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno)
                                        || IsTransientNetworkError(ex.InternalErrno))
        {
            return false;
        }

        using var poller = new Poller();
        var events = new PollEvent[1];
        poller.Add(receiver, PollEvents.PollIn);
        long deadlineTicks = DeadlineTicksFromMilliseconds(Math.Max(1000,
            recvTimeoutMs));
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int timeoutMs = Math.Max(1,
                (int)Math.Ceiling((deadlineTicks - Stopwatch.GetTimestamp())
                    * 1000.0 / Stopwatch.Frequency));
            if (!WaitForInput(poller, events, timeoutMs))
                continue;

            while (TryReceive(receiver, out Received? received))
            {
                using (received)
                {
                    if (!TryGetPayloadPart(received, out Message payloadPart))
                        continue;

                    return TryDecodeExpectedSingleHeader(
                        payloadPart.AsReadOnlySpan(), msgSize, ReadyPhase,
                        out _, RunId);
                }
            }

        }

        return false;
    }

    private static bool RunActivePhase(DealerSocket sender, RouterSocket receiver,
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

                    while (TryReceive(receiver, out Received? receivedMessage))
                    {
                        using (receivedMessage)
                        {
                            if (!TryGetPayloadPart(receivedMessage,
                                    out Message payloadMessage))
                            {
                                continue;
                            }

                            ReadOnlySpan<byte> body = payloadMessage.AsReadOnlySpan();
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
                            lastRecvTicks = Stopwatch.GetTimestamp();
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                recvError = ex;
                DebugLog($"single_dealer_router_error:recv_thread:{ex}");
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
                PerfSocketIo.Send(sender, payload, SendFlags.None);
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
        {
            if (sendFailed)
                DebugLog("single_dealer_router_error:send_failed");
            return false;
        }

        return received > 0 && latencySamples.Count > 0;
    }

    private static bool TryReceive(RouterSocket receiver,
        out Received? receivedMessage)
    {
        try
        {
            receivedMessage = receiver.Recv(RecvFlags.DontWait);
            return true;
        }
        catch (ZlinkRecvException ex)
        {
            receivedMessage = null;
            return false;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno))
        {
            receivedMessage = null;
            return false;
        }
    }

    private static bool TryGetPayloadPart(Received received,
        out Message payloadPart)
    {
        if (received.IsSinglePart)
        {
            payloadPart = received.FirstPart();
            return true;
        }

        if (received.RoutingId != null && received.Parts.Count > 0)
        {
            payloadPart = received.Parts[received.Parts.Count - 1];
            return true;
        }

        payloadPart = default!;
        return false;
    }

    private static bool IsInterrupted(int errno)
    {
        return PerfShared.IsInterrupted(errno);
    }

    private static bool IsWouldBlock(int errno)
    {
        return PerfShared.IsWouldBlock(errno);
    }

    private static bool IsTransientNetworkError(int errno)
    {
        return PerfShared.IsTransientNetworkError(errno);
    }
}
