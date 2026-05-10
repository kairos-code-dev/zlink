using System;
using System.Diagnostics;
using System.Text;
using Systems.Zlink;

internal static partial class PerfRunner
{
    // Wire-level stop token shared with single perf and other bindings.
    // PERF_MULTI_TEST_POLICY § 1.3.1 / PERF_SINGLE_TEST_POLICY § 1.4.
    internal static readonly byte[] MultiStopToken = StopToken.Bytes;

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

    // PERF_MULTI_TEST_POLICY § 1.3.1: client/server poller wait timeouts
    // are unconditionally -1 (signal-driven wait). Deadlines are tracked
    // by application clock; phase end / shutdown is signaled either by
    // stdin STOP (server) or wire-level stop token (client/receiver).
    internal const int MultiClientPollTimeoutMs = -1;

    internal static int ResolveMultiClientPollTimeoutMs(PerfOptions options)
    {
        _ = options;
        return MultiClientPollTimeoutMs;
    }

    internal static int ResolveMultiSpotRouteWarmupMs()
    {
        return PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_ROUTE_WARMUP_MS", 0);
    }

    internal static int ResolveMultiSpotControlStabilizeMs()
    {
        return PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_CONTROL_STABILIZE_MS",
            1000);
    }

    internal static int ResolveMultiSpotControlSettleMs()
    {
        return PerfEnv.ReadNonNegative("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25);
    }

    internal static string MultiEndpointFor(string transport, string name,
        PerfOptions options)
    {
        int bindPort = options.ServerBindPort;
        if (bindPort > 0)
            return $"{transport}://127.0.0.1:{bindPort}";
        return EndpointFor(transport, name);
    }

    internal static string MultiSpotChannelName(string registryEndpoint)
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
        return StopToken.IsStopToken(payload);
    }

    internal static bool ManualSocketOverridesEnabled()
    {
        return PerfEnv.ReadPositive("PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES", 0) > 0
            || PerfEnv.ReadPositive("PERF_ALLOW_MANUAL_SOCKET_OVERRIDES", 0) > 0;
    }

    internal static AutoHwmProfile ResolveContextAutoHwmProfile()
    {
        string value = PerfEnv.ReadString("PERF_CTX_AUTO_HWM_PROFILE", string.Empty);
        if (string.IsNullOrWhiteSpace(value))
            value = PerfEnv.ReadString("PERF_AUTO_HWM_PROFILE", string.Empty);

        return value switch
        {
            "compact" => AutoHwmProfile.Compact,
            "low_latency" or "low-latency" => AutoHwmProfile.LowLatency,
            "throughput" => AutoHwmProfile.Throughput,
            _ => AutoHwmProfile.Balanced,
        };
    }

    internal static void ApplyMultiServerContextOptions(Context ctx,
        PerfOptions options)
    {
        if (options.IoThreads > 0)
            ctx.Options.IoThreads = options.IoThreads;

        if (options.MaxSockets > 0)
            ctx.Options.MaxSockets = options.MaxSockets;

        ctx.Options.Blocky = PerfEnv.ReadBool("PERF_CTX_BLOCKY", false);
        ctx.Options.AutoHwmEnabled = true;
        ctx.Options.AutoHwmProfile = ResolveContextAutoHwmProfile();
    }

    internal static void ApplyMultiClientContextOptions(Context ctx,
        PerfOptions options)
    {
        ApplyMultiServerContextOptions(ctx, options);
    }

    internal static void ApplyMultiSocketOptions(SocketBase socket,
        PerfOptions options)
    {
        int sndTimeo = ResolveMultiSndTimeoutMs(options);
        int rcvTimeo = ResolveMultiRcvTimeoutMs(options);

        socket.Options.Linger = TimeSpan.Zero;
        if (ManualSocketOverridesEnabled())
        {
            int sndHwm = options.ResolveMultiHwm("PERF_MULTI_SNDHWM");
            int rcvHwm = options.ResolveMultiHwm("PERF_MULTI_RCVHWM");
            socket.Options.SendHighWaterMark = Math.Max(1, sndHwm);
            socket.Options.ReceiveHighWaterMark = Math.Max(1, rcvHwm);
        }
        socket.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeo);
        socket.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeo);
    }

    internal static void ApplyAutoHwmMsgUnit(SocketBase socket, int msgSize)
    {
        if (msgSize <= 0)
            return;
        try
        {
            socket.Options.AutoHwmMessageUnitBytes = msgSize;
        }
        catch (ZlinkException)
        {
        }
    }

    internal static void RecalculateAutoHwm(Context ctx)
    {
        try
        {
            ctx.RecalculateAutoHwm();
        }
        catch (ZlinkException)
        {
        }
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
