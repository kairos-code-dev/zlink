using System;
using System.Diagnostics;
using Zlink;

internal static partial class PerfRunner
{
    internal const int SingleConnectWaitMs = 300;

    internal static int ReceiveBlocking(SocketBase socket, Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        while (true)
        {
            try
            {
                return socket.Receive(buffer, flags);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            && (flags & ReceiveFlags.DontWait) != 0)
            {
                return 0;
            }
        }
    }

    internal static int ReceiveBlocking(SocketBase socket, byte[] buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return ReceiveBlocking(socket, buffer.AsSpan(), flags);
    }

    internal static int TryReceiveNonBlocking(SocketBase socket, Span<byte> buffer)
    {
        while (true)
        {
            try
            {
                return socket.Receive(buffer, ReceiveFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.Errno))
            {
                continue;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
            {
                return 0;
            }
        }
    }

    internal static int DrainRemainingFramesNonBlocking(SocketBase socket)
    {
        int drained = 0;
        Span<byte> discard = stackalloc byte[256];
        while (socket.GetOption(SocketOptions.RcvMore) != 0)
        {
            int n = TryReceiveNonBlocking(socket, discard);
            if (n <= 0)
                break;
            drained += n;
        }

        return drained;
    }

    internal static int SendBlocking(SocketBase socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        return socket.Send(buffer, flags);
    }

    internal static int SendBlocking(SocketBase socket, byte[] buffer,
        SendFlags flags = SendFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return socket.Send(buffer, flags);
    }

    internal static bool WaitForInput(Poller poller, Span<PollEvent> events,
        int timeoutMs)
    {
        int written = poller.Wait(events, timeoutMs, out int totalReady);
        return written > 0 || totalReady > 0;
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double thr, double latNs)
    {
        PrintResult(pattern, transport, size, thr, latNs, latNs, latNs);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double thr, double latNs, double latP95Ns, double latP99Ns)
    {
        double bw = BandwidthMbps(thr, size);
        double latMs = NsToMs(latNs);
        double latP95Ms = NsToMs(latP95Ns);
        double latP99Ms = NsToMs(latP99Ns);
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},throughput,{thr}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},bandwidth,{bw}");
        Console.WriteLine($"RESULT,current,{pattern},{transport},{size},latency,{latMs}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p95,{latP95Ms}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},latency_p99,{latP99Ms}");
    }

    private static double BandwidthMbps(double throughput, int size)
    {
        return (throughput * size) / 1_000_000.0;
    }

    private static double NsToMs(double latencyNs)
    {
        return latencyNs / 1_000_000.0;
    }

    private static int ResolveSingleHwmValue(string specificName)
    {
        int hwm = PerfEnv.ReadPositive("PERF_SINGLE_HWM", 1000);
        int specific = PerfEnv.ReadPositive(specificName, 0);
        return specific > 0 ? specific : hwm;
    }

    private static int ResolveSingleMaxSockets()
    {
        int explicitMaxSockets = PerfEnv.ReadPositive("PERF_MAX_SOCKETS", 0);
        if (explicitMaxSockets > 0)
            return explicitMaxSockets;

        int clients = PerfEnv.ReadPositive("PERF_CLIENTS", 0);
        if (clients <= 0)
            return 0;

        long required = clients + 4096L;
        if (required > int.MaxValue)
            return int.MaxValue;
        return (int)required;
    }

    internal static void ApplySingleContextOptions(Context ctx)
    {
        int ioThreads = PerfEnv.ReadNonNegative("PERF_IO_THREADS", 0);
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);

        int maxSockets = ResolveSingleMaxSockets();
        if (maxSockets > 0)
            ctx.SetOption(ContextOption.MaxSockets, maxSockets);
    }

    internal static void ApplySingleSocketOptions(SocketBase socket)
    {
        int sndHwm = ResolveSingleHwmValue("PERF_SINGLE_SNDHWM");
        int rcvHwm = ResolveSingleHwmValue("PERF_SINGLE_RCVHWM");

        int sndTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
        int rcvTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);

        socket.SetOption(SocketOptions.Linger, 0);
        socket.SetOption(SocketOptions.SndHwm, sndHwm);
        socket.SetOption(SocketOptions.RcvHwm, rcvHwm);
        socket.SetOption(SocketOptions.SndTimeo, sndTimeo);
        socket.SetOption(SocketOptions.RcvTimeo, rcvTimeo);
    }

    internal static int ResolveSingleWarmupCount(string pattern)
    {
        int fallback = pattern.Equals("SPOT", StringComparison.OrdinalIgnoreCase)
            ? 200
            : 1000;
        return PerfEnv.ReadPositive("PERF_WARMUP_COUNT", fallback);
    }

    internal static int ResolveSingleDurationSeconds()
    {
        return PerfEnv.ReadPositive("PERF_SINGLE_DURATION_SECONDS", 5);
    }

    internal static int ResolveSingleRcvTimeoutMs()
    {
        return PerfEnv.ReadNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
    }

    internal static int ResolveSingleLatencyCount(string pattern)
    {
        int fallback = pattern.Equals("SPOT", StringComparison.OrdinalIgnoreCase)
            ? 200
            : 500;
        return PerfEnv.ReadPositive("PERF_LAT_COUNT", fallback);
    }

    internal static int ResolveSpotDiscoveryTimeoutMs()
    {
        return PerfEnv.ReadNonNegative("PERF_SPOT_DISCOVERY_TIMEOUT_MS", 4000);
    }

    internal static int ResolveSpotReadyTimeoutMs()
    {
        return PerfEnv.ReadNonNegative("PERF_SPOT_READY_TIMEOUT_MS", 2000);
    }

}
