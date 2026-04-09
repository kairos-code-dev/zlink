using System;
using System.Diagnostics;
using Zlink;

internal static partial class PerfRunner
{
    internal enum PerfRecvMode
    {
        Recv = 0,
    }

    internal static readonly byte[] MultiStopToken =
        System.Text.Encoding.ASCII.GetBytes("__zlink_perf_stop__");

    internal const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    internal enum PerfPhase : uint
    {
        Unknown = 0,
        Warmup = 1,
        Active = 2,
    }

    internal static string NormalizePerfPattern(string pattern)
    {
        return PerfShared.NormalizePattern(pattern, trimMultiPrefix: true);
    }

    internal static bool IsMultiStreamPattern(string pattern)
    {
        return NormalizePerfPattern(pattern) == "STREAM";
    }

    internal static PerfRecvMode ResolveMultiRecvMode(PerfOptions options,
        string pattern)
    {
        _ = options;
        _ = pattern;
        return PerfRecvMode.Recv;
    }

    internal static int ResolveClients(PerfOptions options)
    {
        return options.Clients;
    }

    internal static int ResolveMultiClients(PerfOptions options)
    {
        return ResolveClients(options);
    }

    internal static int ResolveWarmupSeconds(PerfOptions options)
    {
        return options.WarmupSeconds;
    }

    internal static int ResolveMultiWarmupSeconds(PerfOptions options)
    {
        return ResolveWarmupSeconds(options);
    }

    internal static int ResolveDurationSeconds(PerfOptions options)
    {
        return options.DurationSeconds;
    }

    internal static int ResolveMultiDurationSeconds(PerfOptions options)
    {
        return ResolveDurationSeconds(options);
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

    internal static int ResolveStreamIoTimeoutMs(PerfOptions options)
    {
        return options.StreamTimeoutMs;
    }

    internal static int ResolveMultiClientPollTimeoutMs(PerfOptions options)
    {
        return options.ClientPollTimeoutMs;
    }

    internal static int ResolveEffectiveMultiClientPollTimeoutMs(
        PerfOptions options)
    {
        return Math.Max(0, ResolveMultiClientPollTimeoutMs(options));
    }

    internal static int ResolveHwm(PerfOptions options)
    {
        return options.MultiHwm;
    }

    internal static string MultiEndpointFor(string transport, string name,
        PerfOptions options)
    {
        int bindPort = options.ServerBindPort;
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    internal static bool IsMonitorReady(SocketEvent eventValue,
        bool acceptFallback)
    {
        if (eventValue == SocketEvent.ConnectionReady)
            return true;
        if (!acceptFallback)
            return false;
        return eventValue == SocketEvent.Accepted
            || eventValue == SocketEvent.Connected;
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
                return true;

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
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
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

        int readyCount = DrainReadyEvents(monitor, true);
        if (readyCount >= expectedReady)
            return true;

        using var pollManager = new PollManager();
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return true;

            int rc = PollMonitorHandles(pollManager,
                new System.Collections.Generic.List<MonitorSocket> { monitor },
                new[] { 0 }, 1, deadlineTicks, nowTicks);
            if (rc < 0)
                return false;
            if (rc == 0)
                continue;

            try
            {
                readyCount += DrainReadyEvents(monitor, true);
                if (readyCount >= expectedReady)
                    return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
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

    internal static int ResolveMultiHwmValue(string specificName,
        PerfOptions options)
    {
        return options.ResolveMultiHwm(specificName);
    }

    internal static int ResolveIoThreads(PerfOptions options)
    {
        return options.IoThreads;
    }

    internal static int ResolveMultiMaxSockets(PerfOptions options)
    {
        return options.MaxSockets;
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
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", options);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", options);
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

    internal readonly struct ServerQueueStats
    {
        internal ServerQueueStats(double sndPendingMax, double rcvPendingMax,
            double rcvPendingEnd)
        {
            SndPendingMax = sndPendingMax;
            RcvPendingMax = rcvPendingMax;
            RcvPendingEnd = rcvPendingEnd;
        }

        internal double SndPendingMax { get; }
        internal double RcvPendingMax { get; }
        internal double RcvPendingEnd { get; }
    }

    internal static ServerQueueStats SampleServerQueueStats(SocketBase socket)
    {
        PeerRecord[] peers = socket.GetPeers();
        if (peers.Length == 0)
            return new ServerQueueStats(0, 0, 0);

        PeerRecord best = peers[0];
        ulong bestActivity = best.MsgsSent + best.MsgsReceived;
        for (int i = 1; i < peers.Length; i++)
        {
            PeerRecord candidate = peers[i];
            ulong candidateActivity = candidate.MsgsSent + candidate.MsgsReceived;
            if (candidate.ConnectedTime > best.ConnectedTime
                || (candidate.ConnectedTime == best.ConnectedTime
                    && candidateActivity > bestActivity))
            {
                best = candidate;
                bestActivity = candidateActivity;
            }
        }

        double sndPending = best.SndPendingMsgs;
        double rcvPending = best.RcvPendingMsgs;
        return new ServerQueueStats(sndPending, rcvPending, rcvPending);
    }

    internal static void PrintServerQueueMetrics(string pattern, string transport,
        int size, ServerQueueStats stats)
    {
        _ = pattern;
        _ = transport;
        _ = size;
        _ = stats;
    }
}
