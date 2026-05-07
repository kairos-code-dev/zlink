using System;
using System.Diagnostics;
using System.Text;
using Systems.Zlink;

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

    internal static int ResolveMultiDurationSeconds(PerfOptions options)
    {
        return options.DurationSeconds;
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

    internal static int ResolveMultiSpotRouteWarmupMs()
    {
        return PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_ROUTE_WARMUP_MS", 0);
    }

    internal static string MultiEndpointFor(string transport, string name,
        PerfOptions options)
    {
        int bindPort = options.ServerBindPort;
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    internal static string MultiSpotServiceName(string registryEndpoint)
    {
        var builder = new StringBuilder("bench-svc");
        foreach (char ch in registryEndpoint)
        {
            if (char.IsLetterOrDigit(ch))
            {
                builder.Append(char.ToLowerInvariant(ch));
            }
            else if (builder[^1] != '-')
            {
                builder.Append('-');
            }
        }

        return builder.ToString().TrimEnd('-');
    }

    internal static bool IsMonitorReady(MonitorEventType eventValue)
    {
        return eventValue == (MonitorEventType)SocketEvent.ConnectionReady;
    }

    internal static bool WaitConnectReadyCount(MonitorSocket monitor,
        int expectedReady, int timeoutMs)
    {
        if (expectedReady <= 0)
            return true;

        int readyCount = DrainReadyEvents(monitor);
        if (readyCount >= expectedReady)
            return true;

        using var readyPoller = new MonitorReadyPoller();
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return false;

            int rc = readyPoller.Poll(
                new System.Collections.Generic.List<MonitorSocket> { monitor },
                new[] { 0 }, 1, deadlineTicks, nowTicks);
            if (rc < 0)
                return false;
            if (rc == 0)
                continue;

            try
            {
                readyCount += DrainReadyEvents(monitor);
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
            ctx.Options.IoThreads = options.IoThreads;

        if (options.MaxSockets > 0)
            ctx.Options.MaxSockets = options.MaxSockets;
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

        socket.Options.Linger = TimeSpan.Zero;
        socket.Options.SendHighWaterMark = sndHwm;
        socket.Options.ReceiveHighWaterMark = rcvHwm;
        socket.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeo);
        socket.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeo);
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
