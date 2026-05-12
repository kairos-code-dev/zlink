using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using Systems.Zlink;

internal static partial class PerfRunner
{
    internal delegate bool PayloadHandler(ReadOnlySpan<byte> payload);

    // Thread-local Received storage so the helper can use the canonical
    // caller-provided-storage recv shape without forcing callers to thread a
    // Received instance through every helper.
    [ThreadStatic]
    private static Received? t_helperReceived;

    internal static int ReceiveBlocking(SocketBase socket, Span<byte> buffer,
        RecvFlags flags = RecvFlags.None)
    {
        Received reusable = t_helperReceived ??= new Received();
        while (true)
        {
            try
            {
                if (socket is MessageSocketBase messageSocket)
                {
                    if (!messageSocket.Recv(reusable, flags))
                        return 0;
                    return CopyReceivedToBuffer(reusable, buffer);
                }

                if (socket is RoutedMessageSocketBase routedSocket)
                {
                    if (!routedSocket.Recv(reusable, flags))
                        return 0;
                    return CopyReceivedToBuffer(reusable, buffer);
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
            int maxEvents = Math.Max(1, poller.Size);
            var ready = new PollEvent[maxEvents];
            int written = poller.Wait(ready, TimeSpan.FromMilliseconds(timeoutMs),
                out _);
            for (int i = 0; i < written; i++)
                events.Add(ready[i]);
            return written > 0;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno)
                                        || ex.InternalErrno == 0)
        {
            return false;
        }
    }

    internal static int SendBlocking(SocketBase socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        return PerfSocketIo.Send(socket, buffer, flags);
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
        return PerfShared.IsTransientNetworkError(errno);
    }

}
