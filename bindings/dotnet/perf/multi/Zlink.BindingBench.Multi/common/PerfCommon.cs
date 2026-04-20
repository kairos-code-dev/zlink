using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using Zlink;

internal static partial class PerfRunner
{
    internal delegate bool PayloadHandler(ReadOnlySpan<byte> payload);

    internal static int ReceiveBlocking(SocketBase socket, Span<byte> buffer,
        RecvFlags flags = RecvFlags.None)
    {
        while (true)
        {
            try
            {
                if (socket is MessageSocketBase messageSocket)
                {
                    if ((flags & RecvFlags.DontWait) != 0)
                    {
                        if (!messageSocket.TryRecv(out Received? maybe)
                            || maybe == null)
                        {
                            return 0;
                        }

                        using (maybe)
                            return CopyReceivedToBuffer(maybe, buffer);
                    }

                    using Received received = messageSocket.Recv(flags);
                    return CopyReceivedToBuffer(received, buffer);
                }

                if (socket is RoutedMessageSocketBase routedSocket)
                {
                    if ((flags & RecvFlags.DontWait) != 0)
                    {
                        if (!routedSocket.TryRecv(out Received? maybe)
                            || maybe == null)
                        {
                            return 0;
                        }

                        using (maybe)
                            return CopyReceivedToBuffer(maybe, buffer);
                    }

                    using Received received = routedSocket.Recv(flags);
                    return CopyReceivedToBuffer(received, buffer);
                }

                throw new NotSupportedException(
                    $"Unsupported socket type for perf receive: {socket.GetType().Name}");
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || ex.InternalErrno == 0)
            {
                if ((flags & RecvFlags.DontWait) != 0)
                    return 0;
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || ex.InternalErrno == 0)
            {
                if ((flags & RecvFlags.DontWait) != 0)
                    return 0;
                throw;
            }
        }
    }

    internal static int TryReceiveNonBlocking(SocketBase socket,
        Span<byte> buffer)
    {
        return ReceiveBlocking(socket, buffer, RecvFlags.DontWait);
    }

    internal static int ReceiveRetry(SocketBase socket, Span<byte> buffer,
        RecvFlags flags = RecvFlags.None)
    {
        try
        {
            return ReceiveBlocking(socket, buffer, flags);
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno)
                                        || ex.InternalErrno == 0)
        {
            return 0;
        }
    }

    internal static int DrainReadableSocket(SocketBase socket, Span<byte> buffer,
        PayloadHandler onMessage)
    {
        int count = 0;
        while (true)
        {
            int n = TryReceiveNonBlocking(socket, buffer);
            if (n <= 0)
                break;

            count++;
            if (!onMessage(buffer.Slice(0, n)))
                break;
        }

        return count;
    }

    private static int CopyReceivedToBuffer(Received received, Span<byte> buffer)
    {
        Message payload = received.IsSinglePart
            ? received.FirstPart()
            : received.Parts[received.Parts.Count - 1];
        ReadOnlySpan<byte> body = payload.AsReadOnlySpan();
        if (body.Length > buffer.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(buffer));
        }

        body.CopyTo(buffer);
        return body.Length;
    }

    internal static bool WaitForEvents(Poller poller, List<PollEvent> events,
        int timeoutMs)
    {
        events.Clear();
        try
        {
            return poller.Wait(events, timeoutMs) > 0;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno)
                                        || ex.InternalErrno == 0)
        {
            return false;
        }
    }

    internal static int SendBlocking(SocketBase socket, ReadOnlySpan<byte> buffer,
        PerfSendFlags flags = PerfSendFlags.None)
    {
        return socket.Send(buffer, flags);
    }

    internal static bool IsEchoPattern(string pattern)
    {
        string normalized = NormalizePerfPattern(pattern);
        return normalized == "DEALER_ROUTER"
            || normalized == "ROUTER_ROUTER"
            || normalized == "STREAM";
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyNs)
    {
        PrintResult(pattern, transport, size, throughput, latencyNs, latencyNs,
            latencyNs);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns)
    {
        PerfShared.PrintResult(pattern, transport, size, throughput, latencyNs,
            latencyP95Ns, latencyP99Ns, BandwidthMultiplier(pattern),
            fixedFormat: true);
    }

    private static double BandwidthMultiplier(string pattern)
    {
        return IsEchoPattern(pattern) ? 2.0 : 1.0;
    }

    internal static bool StampMetricHeader(Span<byte> payload, uint runId,
        PerfPhase phase, int msgSize, ulong seq, ulong sentTsNs)
    {
        return PerfShared.StampMetricHeader(payload, runId, (uint)phase,
            msgSize, seq, sentTsNs);
    }

    internal static bool TryDecodeMetricHeader(ReadOnlySpan<byte> payload,
        out PerfMetricHeader header)
    {
        return PerfShared.TryDecodeMetricHeader(payload, out header);
    }

    internal static bool IsTransientNetworkError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.EHostUnreach
               || code == ErrorCode.ENetUnreach
               || code == ErrorCode.ENotConn
               || code == ErrorCode.EConnRefused;
    }

}
