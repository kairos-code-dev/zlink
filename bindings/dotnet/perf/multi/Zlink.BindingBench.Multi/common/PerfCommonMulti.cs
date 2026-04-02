using System;
using System.Diagnostics;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal enum PerfRecvMode
    {
        Recv = 0,
        Callback = 1,
    }

    internal static readonly byte[] MultiStopToken =
        System.Text.Encoding.ASCII.GetBytes("__zlink_perf_stop__");

    internal const int MaxStreamFrameBytes = 16 * 1024 * 1024;

    internal static string NormalizePerfPattern(string pattern)
    {
        string normalized = (pattern ?? string.Empty).Trim()
            .ToUpperInvariant();
        const string multiPrefix = "MULTI_";
        if (normalized.StartsWith(multiPrefix, StringComparison.Ordinal))
            return normalized.Substring(multiPrefix.Length);
        return normalized;
    }

    internal static bool IsMultiStreamPattern(string pattern)
    {
        string normalized = NormalizePerfPattern(pattern);
        return normalized == "STREAM"
            || normalized == "STREAM_CALLBACK";
    }

    internal static PerfRecvMode ResolveMultiRecvMode(string pattern)
    {
        string raw = (Environment.GetEnvironmentVariable("PERF_RECV_MODE")
                      ?? string.Empty).Trim();
        if (raw.Length == 0)
            return PerfRecvMode.Recv;

        if (raw.Equals("recv", StringComparison.OrdinalIgnoreCase))
            return PerfRecvMode.Recv;
        if (raw.Equals("callback", StringComparison.OrdinalIgnoreCase))
        {
            string normalized = NormalizePerfPattern(pattern);
            if (normalized == "SPOT" || normalized == "STREAM")
                return PerfRecvMode.Callback;
            throw new ArgumentException(
                $"callback recv mode is unsupported for pattern {normalized}.",
                nameof(pattern));
        }

        throw new ArgumentException(
            $"Unknown recv mode '{raw}'. Expected recv or callback.",
            nameof(pattern));
    }

    internal static int ParseFirstPositiveEnv(int fallback,
        params string[] variableNames)
    {
        foreach (string name in variableNames)
        {
            int parsed = ParsePositiveEnv(name, -1);
            if (parsed > 0)
                return parsed;
        }

        return fallback;
    }

    internal static int ParseFirstNonNegativeEnv(int fallback,
        params string[] variableNames)
    {
        foreach (string name in variableNames)
        {
            int parsed = ParseNonNegativeEnv(name, -1);
            if (parsed >= 0)
                return parsed;
        }

        return fallback;
    }

    internal static int ResolveMultiDrainMs(string pattern)
    {
        _ = pattern;
        return 0;
    }

    internal static int ResolveClients(string pattern)
    {
        int fallback = IsMultiStreamPattern(pattern) ? 10000 : 100;
        return Math.Max(1, ParseFirstPositiveEnv(fallback, "PERF_CLIENTS"));
    }

    internal static int ResolveMultiClients(string pattern)
    {
        return ResolveClients(pattern);
    }

    internal static int ResolveWarmupSeconds()
    {
        return ParseFirstNonNegativeEnv(2, "PERF_WARMUP_SECONDS");
    }

    internal static int ResolveMultiWarmupSeconds()
    {
        return ResolveWarmupSeconds();
    }

    internal static int ResolveDurationSeconds()
    {
        return ParseFirstPositiveEnv(5, "PERF_DURATION_SECONDS");
    }

    internal static int ResolveMultiDurationSeconds()
    {
        return ResolveDurationSeconds();
    }

    internal static int ResolveSettleMs()
    {
        return ParseFirstNonNegativeEnv(500, "PERF_SETTLE_MS");
    }

    internal static int ResolveMultiSettleMs()
    {
        return ResolveSettleMs();
    }

    internal static int ResolveMultiSizeTransitionDrainMs()
    {
        return 0;
    }

    internal static bool ResolveMultiActiveWarmup()
    {
        return ParseNonNegativeEnv("PERF_ACTIVE_WARMUP", 0) == 1;
    }

    internal static int ResolveMultiWarmupDrainMs(int drainMs)
    {
        _ = drainMs;
        return 0;
    }

    internal static int ResolveMultiSndTimeoutMs()
    {
        return ParseFirstPositiveEnv(200, "PERF_SNDTIMEO_MS");
    }

    internal static int ResolveMultiRcvTimeoutMs()
    {
        return ParseFirstPositiveEnv(200, "PERF_RCVTIMEO_MS");
    }

    internal static int ResolveMultiConnectReadyTimeoutMs()
    {
        return ParseFirstPositiveEnv(200, "PERF_CONNECT_READY_TIMEOUT_MS");
    }

    internal static int ResolveStreamIoTimeoutMs()
    {
        return ParseFirstNonNegativeEnv(5000, "PERF_STREAM_TIMEOUT_MS");
    }

    internal static int ResolveMultiClientPollTimeoutMs()
    {
        return ParseFirstNonNegativeEnv(0, "PERF_CLIENT_POLL_TIMEOUT_MS");
    }

    internal static int ResolveEffectiveMultiClientPollTimeoutMs()
    {
        // Perf clients should default to busy-poll semantics so poll timeout
        // does not dominate the measured round-trip throughput.
        return Math.Max(0, ResolveMultiClientPollTimeoutMs());
    }

    internal static int ResolveHwm(string pattern)
    {
        return IsMultiStreamPattern(pattern) ? 10 : 100;
    }

    internal static string MultiEndpointFor(string transport, string name)
    {
        int bindPort = ParseFirstNonNegativeEnv(0, "PERF_SERVER_BIND_PORT");
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

        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return true;

            int rc = PollMonitorHandles(new List<MonitorSocket> { monitor },
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
            catch (ZlinkException ex) when (ex.Errno == ErrnoEagain
                                            || ex.Errno == ErrnoEintr)
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

        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return true;

            int rc = PollMonitorHandles(new List<MonitorSocket> { monitor },
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
            catch (ZlinkException ex) when (ex.Errno == ErrnoEagain
                                            || ex.Errno == ErrnoEintr)
            {
            }
            catch
            {
                return false;
            }
        }

    }

    internal static long DeadlineTicksFromMilliseconds(int timeoutMs)
    {
        long boundedMs = Math.Max(1, timeoutMs);
        long deltaTicks = (boundedMs * Stopwatch.Frequency) / 1000;
        if (deltaTicks <= 0)
            deltaTicks = 1;
        return Stopwatch.GetTimestamp() + deltaTicks;
    }

    internal static bool IsStopTokenPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == MultiStopToken.Length
            && payload.SequenceEqual(MultiStopToken);
    }

    internal static int ResolveMultiHwmValue(string specificName,
        string pattern = "")
    {
        int fallbackHwm = ResolveHwm(pattern);
        int hwm = ParseFirstNonNegativeEnv(fallbackHwm, "PERF_HWM");
        int specific = ParseFirstNonNegativeEnv(0, specificName);
        return specific > 0 ? specific : hwm;
    }

    internal static int ResolveIoThreads()
    {
        // Align multi perf defaults with the core baseline policy unless the
        // caller explicitly overrides them.
        return ParseFirstPositiveEnv(4, "PERF_IO_THREADS");
    }

    internal static int ResolveMultiMaxSockets()
    {
        int explicitMaxSockets = ParseFirstPositiveEnv(0,
            "PERF_MAX_SOCKETS");
        if (explicitMaxSockets > 0)
            return explicitMaxSockets;

        int clients = ParseFirstPositiveEnv(0, "PERF_CLIENTS");
        if (clients <= 0)
            return 0;

        long required = (clients * 3L) + 4096L;
        if (required > int.MaxValue)
            return int.MaxValue;
        return (int)required;
    }

    internal static void ApplyMultiServerContextOptions(Context ctx)
    {
        int ioThreads = ResolveIoThreads();
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);

        int maxSockets = ResolveMultiMaxSockets();
        if (maxSockets > 0)
            ctx.SetOption(ContextOption.MaxSockets, maxSockets);
    }

    internal static void ApplyMultiClientContextOptions(Context ctx)
    {
        int ioThreads = ResolveIoThreads();
        if (ioThreads > 0)
            ctx.SetOption(ContextOption.IoThreads, ioThreads);

        int maxSockets = ResolveMultiMaxSockets();
        if (maxSockets > 0)
            ctx.SetOption(ContextOption.MaxSockets, maxSockets);
    }

    internal static void ApplyMultiSocketOptions(SocketBase socket,
        string pattern = "")
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        int sndTimeo = ResolveMultiSndTimeoutMs();
        int rcvTimeo = ResolveMultiRcvTimeoutMs();

        socket.SetOption(SocketOptions.Linger, 0);
        socket.SetOption(SocketOptions.SndHwm, sndHwm);
        socket.SetOption(SocketOptions.RcvHwm, rcvHwm);
        socket.SetOption(SocketOptions.SndTimeo, sndTimeo);
        socket.SetOption(SocketOptions.RcvTimeo, rcvTimeo);
    }

    internal static int ResolveMultiLatencySampleCap()
    {
        return ParsePositiveEnv("PERF_LATENCY_SAMPLE_CAP", 200000);
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
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_snd_pending_max,{stats.SndPendingMax:F2}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_rcv_pending_max,{stats.RcvPendingMax:F2}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_rcv_pending_end,{stats.RcvPendingEnd:F2}");
    }

}
