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
                    DebugLog("single_dealer_router_error:connection_not_ready");
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
                    recvTimeoutMs, out bool primerUnsupported))
            {
                if (primerUnsupported)
                {
                    ctx.Shutdown();
                    TryCleanup(sender, receiver, ep);
                    PrintUnsupported("DEALER_ROUTER", transport, size,
                        "dotnet_router_inproc_protocol_error");
                    return 0;
                }
                DebugLog("single_dealer_router_error:primer_failed");
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
                DebugLog("single_dealer_router_error:active_failed");
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
        catch (Exception ex)
        {
            try
            {
                TryCleanup(sender, receiver, ep);
            }
            catch
            {
            }
            if (TryPrintUnsupportedTransportFailure("DEALER_ROUTER", transport,
                    size, ex))
            {
                return 0;
            }
            DebugLog($"single_dealer_router_error:exception:{ex}");
            return 2;
        }
        finally
        {
            receiverMonitor?.Dispose();
            senderMonitor?.Dispose();
        }
    }

    private static bool RunPrimer(DealerSocket sender, RouterSocket receiver,
        int payloadSize, int recvTimeoutMs, out bool unsupportedProtocol)
    {
        unsupportedProtocol = false;
        var payload = new byte[payloadSize];
        Array.Fill(payload, (byte)'p');
        long deadline = DeadlineTicksFromMilliseconds(Math.Max(1000, recvTimeoutMs));
        while (Stopwatch.GetTimestamp() < deadline)
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
                Thread.Sleep(1);
                continue;
            }
            catch (Exception ex)
            {
                DebugLog($"single_dealer_router_error:primer_failure:{ex}");
                return false;
            }

            if (!TryReceive(receiver, out Received? received,
                    out unsupportedProtocol))
            {
                Thread.Sleep(1);
                continue;
            }

            using (received)
            {
                if (!TryGetPayloadPart(received, out Message payloadPart))
                    continue;
                return payloadPart.Size == payloadSize;
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

            try
            {
                while (true)
                {
                    bool done = Volatile.Read(ref senderDone) != 0;
                    if (TryReceive(receiver, out Received? receivedMessage,
                            out _))
                    {
                        using (receivedMessage)
                        {
                            if (!TryGetPayloadPart(receivedMessage,
                                    out Message payloadMessage))
                            {
                                continue;
                            }

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

                        continue;
                    }

                    if (done && Stopwatch.GetTimestamp() - lastRecvTicks
                        >= recvFlushTicks)
                    {
                        break;
                    }

                    Thread.Sleep(1);
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
        {
            if (sendFailed)
                DebugLog("single_dealer_router_error:send_failed");
            return false;
        }

        return received > 0 && latencySamples.Count > 0;
    }

    private static bool TryReceive(RouterSocket receiver,
        out Received? receivedMessage, out bool unsupportedProtocol)
    {
        unsupportedProtocol = false;
        try
        {
            receivedMessage = receiver.Recv(RecvFlags.DontWait);
            return true;
        }
        catch (ZlinkRecvException ex)
        {
            unsupportedProtocol = IsInprocProtocolUnsupported(ex.InternalErrno);
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

    private static bool IsInprocProtocolUnsupported(int errno)
    {
        return errno == 71;
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
