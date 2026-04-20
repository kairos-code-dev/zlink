using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using Zlink;

internal static partial class PerfRunner
{
    internal delegate bool PayloadHandler(ReadOnlySpan<byte> payload);

    internal readonly struct PerfMetricHeader
    {
        internal readonly uint Magic;
        internal readonly uint RunId;
        internal readonly uint Phase;
        internal readonly uint MsgSize;
        internal readonly ulong Seq;
        internal readonly ulong SentTsNs;

        internal PerfMetricHeader(uint magic, uint runId, uint phase,
            uint msgSize, ulong seq, ulong sentTsNs)
        {
            Magic = magic;
            RunId = runId;
            Phase = phase;
            MsgSize = msgSize;
            Seq = seq;
            SentTsNs = sentTsNs;
        }
    }

    internal static int ReceiveBlocking(SocketBase socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        while (true)
        {
            try
            {
                if (socket is MessageSocketBase messageSocket)
                {
                    if ((flags & ReceiveFlags.DontWait) != 0)
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
                    if ((flags & ReceiveFlags.DontWait) != 0)
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
                if ((flags & ReceiveFlags.DontWait) != 0)
                    return 0;
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || ex.InternalErrno == 0)
            {
                if ((flags & ReceiveFlags.DontWait) != 0)
                    return 0;
                throw;
            }
        }
    }

    internal static int TryReceiveNonBlocking(SocketBase socket,
        Span<byte> buffer)
    {
        return ReceiveBlocking(socket, buffer, ReceiveFlags.DontWait);
    }

    internal static int ReceiveRetry(SocketBase socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
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
        double bandwidth = BandwidthMbps(pattern, throughput, size);
        double latencyMs = latencyNs / 1_000_000.0;
        double latencyP95Ms = latencyP95Ns / 1_000_000.0;
        double latencyP99Ms = latencyP99Ns / 1_000_000.0;
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},throughput,{FormatMetric(throughput)}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},bandwidth,{FormatMetric(bandwidth)}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency,{FormatMetric(latencyMs)}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p95,{FormatMetric(latencyP95Ms)}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p99,{FormatMetric(latencyP99Ms)}");
    }

    private static double BandwidthMbps(string pattern, double throughput, int size)
    {
        double multiplier = IsEchoPattern(pattern) ? 2.0 : 1.0;
        return (throughput * size * multiplier) / 1_000_000.0;
    }

    private static string FormatMetric(double value)
    {
        return value.ToString("F3", CultureInfo.InvariantCulture);
    }

    internal static bool StampMetricHeader(Span<byte> payload, uint runId,
        PerfPhase phase, int msgSize, ulong seq, ulong sentTsNs)
    {
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(0, 4),
            PerfMetricMagic);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(4, 4), runId);
        payload[8] = (byte)phase;
        BinaryPrimitives.WriteUInt32LittleEndian(payload.Slice(9, 4),
            (uint)Math.Max(0, msgSize));
        BinaryPrimitives.WriteUInt64LittleEndian(payload.Slice(13, 8), seq);
        BinaryPrimitives.WriteInt64LittleEndian(payload.Slice(21, 8),
            checked((long)sentTsNs));
        return true;
    }

    internal static bool TryDecodeMetricHeader(ReadOnlySpan<byte> payload,
        out PerfMetricHeader header)
    {
        header = default;
        if (payload.Length < PerfMetricHeaderSize)
            return false;

        uint magic = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(0,
            4));
        uint runId = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(4,
            4));
        uint phase = payload[8];
        uint msgSize = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(9,
            4));
        ulong seq = BinaryPrimitives.ReadUInt64LittleEndian(payload.Slice(13,
            8));
        ulong sentTsNs = checked((ulong)BinaryPrimitives.ReadInt64LittleEndian(
            payload.Slice(21, 8)));

        header = new PerfMetricHeader(magic, runId, phase, msgSize, seq,
            sentTsNs);
        return magic == PerfMetricMagic;
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
