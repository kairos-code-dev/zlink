using System;
using System.Diagnostics;
using Zlink;

internal static partial class PerfRunner
{
    internal static readonly byte[] MultiStopToken =
        System.Text.Encoding.ASCII.GetBytes("__zlink_perf_stop__");

    internal const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    internal enum PerfPhase : byte
    {
        Warmup = 0,
        Active = 1,
        Cooldown = 2,
    }

    internal static string NormalizePerfPattern(string pattern)
    {
        return PerfShared.NormalizePattern(pattern, trimMultiPrefix: true);
    }

    internal static bool IsMultiStreamPattern(string pattern)
    {
        return NormalizePerfPattern(pattern) == "STREAM";
    }

    internal static int ResolveMultiClients(PerfOptions options)
    {
        return options.Clients;
    }

    internal static int ResolveMultiWarmupSeconds(PerfOptions options)
    {
        return options.WarmupSeconds;
    }

    internal static int ResolveMultiDurationSeconds(PerfOptions options)
    {
        return options.DurationSeconds;
    }

    internal static bool ResolveMultiActiveWarmup(PerfOptions options)
    {
        return options.ActiveWarmup;
    }

    internal static int ResolveMultiSndTimeoutMs(PerfOptions options)
    {
        return options.SndTimeoutMs;
    }

    internal static int ResolveMultiRcvTimeoutMs(PerfOptions options)
    {
        return options.RcvTimeoutMs;
    }

    internal static int ResolveMultiConnectReadyTimeoutMs(PerfOptions options)
    {
        return options.ConnectReadyTimeoutMs;
    }

    internal static string MultiEndpointFor(string transport, string name,
        PerfOptions options)
    {
        int bindPort = options.ServerBindPort;
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    internal static bool IsMonitorReady(MonitorEventType eventValue,
        bool acceptFallback)
    {
        if (eventValue == (MonitorEventType)SocketEvent.ConnectionReady)
            return true;
        if (!acceptFallback)
            return false;
        return eventValue == (MonitorEventType)SocketEvent.Accepted
            || eventValue == (MonitorEventType)SocketEvent.Connected;
    }

    internal static bool WaitMonitorReady(MonitorSocket monitor, int timeoutMs,
        bool acceptFallback)
    {
        if (DrainReadyEvents(monitor, acceptFallback) > 0)
            return true;

        using var pollManager = new PollManager();
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return false;

            int rc = PollMonitorHandles(pollManager,
                new System.Collections.Generic.List<MonitorSocket> { monitor },
                new[] { 0 }, 1, deadlineTicks, nowTicks);
            if (rc < 0)
                return false;
            if (rc == 0)
                continue;

            try
            {
                if (DrainReadyEvents(monitor, acceptFallback) > 0)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
            catch
            {
                return false;
            }
        }
    }

    internal static bool WaitConnectReadyCount(MonitorSocket monitor,
        int expectedReady, int timeoutMs)
    {
        if (expectedReady <= 0)
            return true;

        int readyCount = DrainReadyEvents(monitor, false);
        if (readyCount >= expectedReady)
            return true;

        using var pollManager = new PollManager();
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return false;

            int rc = PollMonitorHandles(pollManager,
                new System.Collections.Generic.List<MonitorSocket> { monitor },
                new[] { 0 }, 1, deadlineTicks, nowTicks);
            if (rc < 0)
                return false;
            if (rc == 0)
                continue;

            try
            {
                readyCount += DrainReadyEvents(monitor, false);
                if (readyCount >= expectedReady)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
            }
            catch
            {
                return false;
            }
        }
    }

    internal static bool IsStopTokenPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == MultiStopToken.Length
            && payload.SequenceEqual(MultiStopToken);
    }

    internal static void ApplyMultiServerContextOptions(Context ctx,
        PerfOptions options)
    {
        if (options.IoThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, options.IoThreads);

        if (options.MaxSockets > 0)
            ctx.SetOption(ContextOption.MaxSockets, options.MaxSockets);
    }

    internal static void ApplyMultiClientContextOptions(Context ctx,
        PerfOptions options)
    {
        ApplyMultiServerContextOptions(ctx, options);
    }

    internal static void ApplyMultiSocketOptions(SocketBase socket,
        PerfOptions options)
    {
        int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
        int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
        int sndTimeo = ResolveMultiSndTimeoutMs(options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);

        socket.SetOption(SocketOptions.Linger, 0);
        socket.SetOption(SocketOptions.SndHwm, sndHwm);
        socket.SetOption(SocketOptions.RcvHwm, rcvHwm);
        socket.SetOption(SocketOptions.SndTimeo, sndTimeo);
        socket.SetOption(SocketOptions.RcvTimeo, rcvTimeo);
    }

    internal static int ResolveMultiLatencySampleCap(PerfOptions options)
    {
        return options.LatencySampleCap;
    }

    internal static bool IsCoreStreamServerTransport(string transport)
    {
        return transport.Equals("tcp", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("tls", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("ws", StringComparison.OrdinalIgnoreCase)
            || transport.Equals("wss", StringComparison.OrdinalIgnoreCase);
    }

}
