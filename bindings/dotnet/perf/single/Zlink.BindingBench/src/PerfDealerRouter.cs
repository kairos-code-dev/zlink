using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfDealerRouter
{
    private static readonly RoutingId ClientRoutingId =
        RoutingId.FromBytes("CLIENT"u8);
    private const uint RunId = 1;
    private const uint ActivePhase = 1;

    private static bool TryCleanup(DealerSocket sender, RouterSocket receiver,
        string endpoint)
    {
        bool ok = true;
        try
        {
            sender.Disconnect(endpoint);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-dealer-router] cleanup disconnect failed: {ex.Message}");
            ok = false;
        }

        try
        {
            receiver.Unbind(endpoint);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-dealer-router] cleanup unbind failed: {ex.Message}");
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
        ApplySingleAutoHwmMsgUnit(receiver, size);
        ApplySingleAutoHwmMsgUnit(sender, size);
        RecalculateSingleAutoHwm(ctx);
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
            endpoint = receiver.Options.LastEndpoint;
            sender.Connect(endpoint);
            if (!(WaitForConnectionReady(receiverMonitor, readyTimeoutMs)
                && WaitForConnectionReady(senderMonitor, readyTimeoutMs)))
            {
                DebugLog("single_dealer_router_error:connection_not_ready");
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            // PERF_SINGLE_TEST_POLICY § 1.4 / C perf_dealer_router.cpp
            // run_dealer_router: go directly from the CONNECTION_READY gate
            // to the active phase. C defines wait_for_dealer_router_ready but
            // never calls it (only the ROUTER_ROUTER pattern performs a
            // routing self-check / PING-PONG handshake), so no pre-active
            // routing probe is performed here. This keeps the active-start
            // anchor identical to C.
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
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("DEALER_ROUTER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
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

    private static bool RunActivePhase(DealerSocket sender, RouterSocket receiver,
        byte[] payload, int msgSize, int durationSeconds, int recvTimeoutMs,
        int latencyCap, out long receivedOut, out List<double> latencySamples)
    {
        _ = recvTimeoutMs;
        long deadlineTicks = DeadlineTicksFromSeconds(durationSeconds);

        long received = 0;
        Exception? sendError = null;
        var samples = new List<double>(Math.Max(0, latencyCap));
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        bool stopReceived = false;

        bool ProcessReceived(Received receivedMessage)
        {
            if (!TryGetPayloadPart(receivedMessage, out Message payloadMessage))
                return false;

            ReadOnlySpan<byte> body = payloadMessage.AsReadOnlySpan();
            if (StopToken.IsStopToken(body))
                return true;

            if (!TryDecodeExpectedSingleHeader(body, msgSize, ActivePhase,
                    out var header, RunId))
            {
                return false;
            }

            received++;
            ulong nowNs = EpochNs();
            if (nowNs >= header.SentTsNs)
            {
                double latencyNs = nowNs - header.SentTsNs;
                ReservoirSample(samples, latencyNs, ref sampleSeen, latencyCap,
                    ref rng);
            }

            return false;
        }

        // PERF_SINGLE_TEST_POLICY § 1.4: sender no longer flips a
        // `senderDone` flag. After the active deadline it sends the
        // wire-level stop token; the receiver loop exits when it sees
        // the token in the inbound stream.
        var senderThread = new Thread(() =>
        {
            try
            {
                ulong seq = 1;
                while (true)
                {
                    long nowTicks = Stopwatch.GetTimestamp();
                    if (nowTicks >= deadlineTicks)
                        break;
                    StampMetricHeader(payload.AsSpan(), RunId, ActivePhase,
                        msgSize, seq, EpochNsFromTimestamp(nowTicks));
                    seq++;
                    try
                    {
                        if (!TrySendActiveMessage(sender, payload,
                                "[single-dealer-router]"))
                            continue;
                    }
                    catch (ZlinkException ex)
                        when (PerfShared.IsTransientBackpressure(
                                  ex.InternalErrno))
                    {
                        continue;
                    }
                }
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno))
            {
                sendError = ex;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[single-dealer-router] send failed: {ex.Message}");
                sendError = ex;
            }
            finally
            {
                // Always emit the stop token so the receiver loop exits
                // even on send failures during the active phase.
                SendStopTokenBlocking(sender, "[single-dealer-router]");
            }
        });
        senderThread.IsBackground = true;
        senderThread.Start();

        var receivedBuffer = new Received();
        while (!stopReceived)
        {
            if (!TryReceiveBlocking(receiver, receivedBuffer))
                continue;
            if (ProcessReceived(receivedBuffer))
                stopReceived = true;
        }

        senderThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendError != null)
        {
            DebugLog("single_dealer_router_error:send_failed");
            return false;
        }

        return received > 0 && latencySamples.Count > 0;
    }

    private static bool TryReceiveBlocking(RouterSocket receiver, Received result)
    {
        try
        {
            return receiver.Recv(result);
        }
        catch (ZlinkRecvException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
        {
            return false;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno))
        {
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
}
