using System;
using System.Diagnostics;
using Systems.Zlink;

internal static partial class PerfRunner
{
    private static readonly bool SinglePerfDebugEnabled =
        !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("PERF_DEBUG"));

    internal static void DebugLog(string message)
    {
        if (SinglePerfDebugEnabled)
            Console.Error.WriteLine(message);
    }

    internal static bool WaitForConnectionReady(MonitorSocket monitor,
        int timeoutMs)
    {
        using var readyPoller = new MonitorReadyPoller();
        var monitors = new System.Collections.Generic.List<MonitorSocket>
        {
            monitor,
        };
        var activeIndices = new[] { 0 };
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= deadlineTicks)
                return false;

            try
            {
                while (true)
                {
                    MonitorEvent? evt = monitor.Recv(true);
                    if (evt == null)
                        break;
                    if (evt.Event == MonitorEventType.ConnectionReady)
                        return true;
                }
            }
            catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                            || IsWouldBlock(ex.InternalErrno))
            {
            }

            int ready = readyPoller.Poll(monitors, activeIndices, 1,
                deadlineTicks, nowTicks);
            if (ready < 0)
                return false;
        }
    }

    internal static bool IsExpectedSingleHeader(PerfMetricHeader header,
        int msgSize, uint phase, uint runId = 1)
    {
        return header.RunId == runId
            && header.Phase == phase
            && header.MsgSize == (uint)msgSize;
    }

    internal static bool TryDecodeExpectedSingleHeader(ReadOnlySpan<byte> payload,
        int msgSize, uint phase, out PerfMetricHeader header, uint runId = 1)
    {
        header = default;
        if (!TryDecodeMetricHeader(payload, out header))
            return false;
        return IsExpectedSingleHeader(header, msgSize, phase, runId);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double thr, double latNs)
    {
        PrintResult(pattern, transport, size, thr, latNs, latNs, latNs);
    }

    internal static void PrintResult(string pattern, string transport, int size,
        double thr, double latNs, double latP95Ns, double latP99Ns,
        double bandwidthMultiplier = 1.0)
    {
        PerfShared.PrintResult(pattern, transport, size, thr, latNs, latP95Ns,
            latP99Ns, bandwidthMultiplier, fixedFormat: false);
    }

    internal static int ResolveSingleConnectReadyTimeoutMs()
    {
        return PerfEnv.ReadPositive("PERF_CONNECT_READY_TIMEOUT_MS", 1000);
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
        _ = pattern;
        return PerfEnv.ReadPositive("PERF_SINGLE_LATENCY_SAMPLE_CAP", 200000);
    }

    internal static int ResolveSinglePubSubReadySettleMs()
    {
        return PerfEnv.ReadNonNegative("PERF_SINGLE_PUBSUB_READY_SETTLE_MS",
            1000);
    }

    internal static int ResolveSingleSpotReadySettleMs()
    {
        return PerfEnv.ReadNonNegative("PERF_SINGLE_SPOT_READY_SETTLE_MS",
            1000);
    }

    internal static int ResolveSpotReadyTimeoutMs()
    {
        return ResolveSingleConnectReadyTimeoutMs();
    }

    internal static int SendBlocking(SocketBase socket, ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        return PerfSocketIo.Send(socket, buffer, flags);
    }

    internal static int SendBlocking(SocketBase socket, byte[] buffer,
        SendFlags flags = SendFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return PerfSocketIo.Send(socket, buffer, flags);
    }

    internal static bool WaitForInput(Poller poller, Span<PollEvent> events,
        int timeoutMs)
    {
        try
        {
            int written = poller.Wait(events, timeoutMs, out int totalReady);
            return written > 0 || totalReady > 0;
        }
        catch (ZlinkException ex) when (IsInterrupted(ex.InternalErrno)
                                        || IsWouldBlock(ex.InternalErrno))
        {
            return false;
        }
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
        return explicitMaxSockets > 0 ? explicitMaxSockets : 0;
    }

    internal static void ApplySingleContextOptions(Context ctx)
    {
        int ioThreads = PerfEnv.ReadNonNegative("PERF_IO_THREADS", 0);
        if (ioThreads > 0)
            ctx.Options.IoThreads = ioThreads;

        int maxSockets = ResolveSingleMaxSockets();
        if (maxSockets > 0)
            ctx.Options.MaxSockets = maxSockets;
    }

    internal static void ApplySingleSocketOptions(SocketBase socket)
    {
        int sndHwm = ResolveSingleHwmValue("PERF_SINGLE_SNDHWM");
        int rcvHwm = ResolveSingleHwmValue("PERF_SINGLE_RCVHWM");

        int sndTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
        int rcvTimeo = PerfEnv.ReadNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);

        socket.Options.Linger = TimeSpan.Zero;
        socket.Options.SendHighWaterMark = sndHwm;
        socket.Options.ReceiveHighWaterMark = rcvHwm;
        socket.Options.SendTimeout = TimeSpan.FromMilliseconds(sndTimeo);
        socket.Options.ReceiveTimeout = TimeSpan.FromMilliseconds(rcvTimeo);
    }

}
