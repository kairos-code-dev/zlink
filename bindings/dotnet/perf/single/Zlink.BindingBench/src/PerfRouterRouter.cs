using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Systems.Zlink;
using static PerfRunner;

internal static class PerfRouterRouter
{
    private static readonly RoutingId ReceiverRoutingId =
        RoutingId.FromBytes("ROUTER1"u8);
    private static readonly RoutingId SenderRoutingId =
        RoutingId.FromBytes("ROUTER2"u8);
    private const uint RunId = 1;
    private const uint ReadyPhase = 0;
    private const uint ActivePhase = 1;

    private static void TryCleanup(RouterSocket sender, RouterSocket receiver,
        string endpoint)
    {
        try
        {
            sender.Disconnect(endpoint);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-router-router] cleanup disconnect failed: {ex.Message}");
        }

        try
        {
            receiver.Unbind(endpoint);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[single-router-router] cleanup unbind failed: {ex.Message}");
        }
    }

    internal static int RunRouterRouter(string transport, int size)
    {
        int durationSeconds = ResolveSingleDurationSeconds();
        int recvTimeoutMs = ResolveSingleRcvTimeoutMs();
        int readyTimeoutMs = ResolveSingleConnectReadyTimeoutMs();
        int latencySampleCap = ResolveSingleLatencyCount("ROUTER_ROUTER");

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var receiver = new RouterSocket(ctx);
        using var sender = new RouterSocket(ctx);
        ApplySingleSocketOptions(receiver);
        ApplySingleSocketOptions(sender);
        ApplySingleAutoHwmMsgUnit(ctx, size);
        RecalculateSingleAutoHwm(ctx);
        ConfigureTlsServerIfNeeded(receiver, transport);
        ConfigureTlsClientIfNeeded(sender, transport);
        MonitorSocket? receiverMonitor = null;
        MonitorSocket? senderMonitor = null;
        string endpoint = EndpointFor(transport, "router-router");

        try
        {
            receiverMonitor = receiver.MonitorOpen(SocketEvent.ConnectionReady);
            senderMonitor = sender.MonitorOpen(SocketEvent.ConnectionReady);

            receiver.SetRoutingId(ReceiverRoutingId);
            sender.SetRoutingId(SenderRoutingId);
            receiver.Options.Mandatory = true;
            sender.Options.Mandatory = true;
            receiver.Bind(endpoint);
            endpoint = receiver.Options.LastEndpoint;
            sender.Connect(endpoint);
            if (!(WaitForConnectionReady(receiverMonitor, readyTimeoutMs)
                && WaitForConnectionReady(senderMonitor, readyTimeoutMs)))
            {
                DebugLog("single_router_router_error:connection_not_ready");
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            if (!CompleteHandshake(sender, receiver, readyTimeoutMs,
                    out RoutingId targetRoutingId))
            {
                TryCleanup(sender, receiver, endpoint);
                DebugLog("single_router_router_error:route_probe_failed");
                return 2;
            }

            // ITEM 1: capture AUTO_HWM_DETAIL from the live monitors BEFORE
            // they are disposed (C parity; auto-HWM applied values are stable
            // once configured and connection-ready).
            EmitSingleAutoHwmDetail(receiverMonitor, "ROUTER_ROUTER",
                transport, "receiver", "router", size);
            EmitSingleAutoHwmDetail(senderMonitor, "ROUTER_ROUTER",
                transport, "sender", "router", size);

            receiverMonitor.Dispose();
            receiverMonitor = null;
            senderMonitor.Dispose();
            senderMonitor = null;

            int payloadSize = Math.Max(size, PerfMetricHeaderSize);
            var payload = new byte[payloadSize];
            Array.Fill(payload, (byte)'a');

            if (!RunActivePhase(sender, receiver, targetRoutingId, payload,
                    size, durationSeconds, recvTimeoutMs, latencySampleCap,
                    out long received,
                    out var latencySamples))
            {
                DebugLog("single_router_router_error:active_failed");
                TryCleanup(sender, receiver, endpoint);
                return 2;
            }

            double throughput = received / (double)Math.Max(durationSeconds, 1);
            var latency = ComputeLatencyStats(latencySamples);
            PrintResult("ROUTER_ROUTER", transport, size, throughput,
                latency.mean, latency.p95, latency.p99);
            TryCleanup(sender, receiver, endpoint);
            return 0;
        }
        catch (Exception ex)
        {
            TryCleanup(sender, receiver, endpoint);
            DebugLog($"single_router_router_error:exception:{ex}");
            return 2;
        }
        finally
        {
            receiverMonitor?.Dispose();
            senderMonitor?.Dispose();
        }
    }

    private static bool CompleteHandshake(RouterSocket sender,
        RouterSocket receiver, int timeoutMs, out RoutingId targetRoutingId)
    {
        targetRoutingId = ReceiverRoutingId;
        ReadOnlySpan<byte> ping = "PING"u8;
        ReadOnlySpan<byte> pong = "PONG"u8;
        RoutingId? senderActualRoutingId = null;

        using var poller = new Poller();
        var events = new PollEvent[1];
        poller.Add(receiver, PollEventFlags.PollIn);
        long deadlineTicks = DeadlineTicksFromMilliseconds(Math.Max(1000, timeoutMs));
        while (senderActualRoutingId == null
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            try
            {
                _ = PerfSocketIo.Send(sender, ReceiverRoutingId, ping,
                    SendFlags.DontWait);
            }
            catch (ZlinkException ex)
                when (PerfShared.IsTransientBackpressure(ex.InternalErrno)
                      || IsTransientNetworkError(ex.InternalErrno))
            {
            }

            int waitMs = Math.Max(1,
                (int)Math.Ceiling((deadlineTicks - Stopwatch.GetTimestamp())
                    * 1000.0 / Stopwatch.Frequency));
            if (!WaitForInput(poller, events, waitMs))
                continue;

            using var receivedBuffer = new Received();
            while (TryReceive(receiver, receivedBuffer))
            {
                if (receivedBuffer.RoutingId == null
                    || !IsPayload(receivedBuffer, ping))
                    continue;

                senderActualRoutingId = receivedBuffer.RoutingId;
                break;
            }
        }

        if (senderActualRoutingId == null)
            return false;

        try
        {
            if (PerfSocketIo.Send(receiver, senderActualRoutingId.Value, pong,
                    SendFlags.None) == 0)
                return false;
        }
        catch (ZlinkException ex)
            when (PerfShared.IsTransientBackpressure(ex.InternalErrno)
                  || IsTransientNetworkError(ex.InternalErrno))
        {
            return false;
        }

        using var senderPoller = new Poller();
        var senderEvents = new PollEvent[1];
        senderPoller.Add(sender, PollEventFlags.PollIn);
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int waitMs = Math.Max(1,
                (int)Math.Ceiling((deadlineTicks - Stopwatch.GetTimestamp())
                    * 1000.0 / Stopwatch.Frequency));
            if (!WaitForInput(senderPoller, senderEvents, waitMs))
                continue;

            using var response = new Received();
            while (TryReceive(sender, response))
            {
                if (response.RoutingId == null || !IsPayload(response, pong))
                    continue;

                targetRoutingId = response.RoutingId.Value;
                return true;
            }
        }

        return false;
    }

    private static bool RunActivePhase(RouterSocket sender, RouterSocket receiver,
        RoutingId targetRoutingId, byte[] payload, int msgSize,
        int durationSeconds, int recvTimeoutMs, int latencyCap,
        out long receivedOut, out List<double> latencySamples)
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

        // PERF_SINGLE_TEST_POLICY § 1.4: sender drops `senderDone` flag.
        // After active deadline it emits the wire-level stop token; the
        // receiver loop exits when it sees the token.
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
                        if (PerfSocketIo.Send(sender, targetRoutingId, payload,
                                SendFlags.None) == 0)
                            continue;
                    }
                    catch (ZlinkException ex)
                        when (PerfShared.IsTransientBackpressure(
                                  ex.InternalErrno)
                              || IsTransientNetworkError(ex.InternalErrno))
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
                Console.Error.WriteLine($"[single-router-router] send failed: {ex.Message}");
                sendError = ex;
            }
            finally
            {
                // Always emit the stop token so the receiver loop exits
                // even on send failures during the active phase.
                if (!SendRoutedStopTokenBlocking(sender, targetRoutingId,
                        "[single-router-router]"))
                {
                    sendError ??= new TimeoutException(
                        "router-router stop token was not sent");
                }
            }
        });
        senderThread.IsBackground = true;
        senderThread.Start();

        using var poller = new Poller();
        var events = new PollEvent[1];
        poller.Add(receiver, PollEventFlags.PollIn);
        var receivedBuffer = new Received();
        // PERF_SINGLE_TEST_POLICY § 1.4: signal-driven (-1) poller wait, no
        // sender_done atomic flag, no short-poll/deadline fallback. The phase
        // ends purely when the wire-level stop token arrives, matching C
        // perf_router_router.cpp run_active_phase (recv loop exits only on
        // recv_rc == 2 / is_stop_token).
        while (!stopReceived)
        {
            if (!WaitForInputSignalDriven(poller, events))
                continue;

            while (TryReceive(receiver, receivedBuffer))
            {
                if (ProcessReceived(receivedBuffer))
                {
                    stopReceived = true;
                    break;
                }
            }
        }

        senderThread.Join();

        latencySamples = samples;
        receivedOut = received;
        if (sendError != null)
        {
            DebugLog("single_router_router_error:send_failed");
            return false;
        }

        return received > 0 && latencySamples.Count > 0;
    }

    private static bool TryReceive(RouterSocket receiver, Received result)
    {
        try
        {
            return receiver.Recv(result, RecvFlags.DontWait);
        }
        catch (ZlinkRecvException)
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

    private static bool IsPayload(Received received, ReadOnlySpan<byte> expected)
    {
        if (!TryGetPayloadPart(received, out Message payloadPart))
            return false;
        return payloadPart.AsReadOnlySpan().SequenceEqual(expected);
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
