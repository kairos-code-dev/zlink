using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpotServer
{
    internal static int Run(string transport, int size)
    {
        const string pattern = "SPOT";
        const int subscribeSettleMs = 300;
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        if (size >= 65536 && warmupSeconds > 0)
            warmupSeconds = 0;
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int sndTimeoutMs = Math.Min(ResolveMultiSndTimeoutMs(), 200);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        string endpoint = MultiEndpointFor(transport, "multi-spot");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var nodePub = new SpotNode(ctx);
        ConfigureSpotTlsPublisherIfNeeded(nodePub, transport);
        TryConfigureSpotPublisherSocket(nodePub, pattern, sndTimeoutMs);
        nodePub.Bind(endpoint);
        using var spotPub = new Spot(nodePub);
        Console.WriteLine($"READY,{endpoint}");
        _ = WaitUntil(() => nodePub.GetSubPeers().Length > 0, readyTimeoutMs);
        Thread.Sleep(subscribeSettleMs);

        const uint runId = 1;
        ulong seq = 1;
        var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'a');

        if (warmupSeconds > 0)
        {
            long warmupDeadline = Stopwatch.GetTimestamp()
                + (long)warmupSeconds * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < warmupDeadline)
            {
                StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Warmup,
                    size, seq++, EpochUs());
                TryPublishSpot(spotPub, payload.AsSpan());
            }
        }

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        long sendCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active, size,
                seq++, EpochUs());
            if (TryPublishSpot(spotPub, payload.AsSpan()))
                sendCount++;
        }
        long benchEndTicks = Stopwatch.GetTimestamp();

        double elapsedSeconds = (benchEndTicks - benchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? sendCount / elapsedSeconds
            : 0.0;
        double latencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, sendCount);
        PrintResult(pattern, transport, size, throughput, latencyUs,
            latencyUs, latencyUs);
        return 0;
    }

    private static bool TryPublishSpot(Spot spotPub, ReadOnlySpan<byte> payload)
    {
        try
        {
            spotPub.Publish("bench", payload, SendFlags.None);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno)
                                        || IsTransientNetworkError(ex.Errno))
        {
            return false;
        }
    }

    private static void TryConfigureSpotPublisherSocket(SpotNode node,
        string pattern, int sndTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        TrySetSpotNodeOption(node, SpotNodeOption.PubMode,
            (int)SpotNodePubMode.Async);
        TrySetSpotNodeOption(node, SpotNodeOption.PubQueueHwm,
            Math.Max(10, sndHwm));
        TrySetSpotNodeOption(node, SpotNodeOption.PubQueueFullPolicy,
            (int)SpotNodePubQueueFullPolicy.Drop);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Blocky, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndHwm, sndHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndTimeo, sndTimeoutMs);
    }

    private static void TrySetSpotNodeOption(SpotNode node, SpotNodeOption option,
        int value)
    {
        try
        {
            node.SetOption(option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.Errno))
        {
        }
    }

    private static void TrySetSpotNodeSocketOption(SpotNode node,
        SpotNodeSocketRole role, SocketOptionKey<int> option, int value)
    {
        try
        {
            node.SetOption(role, option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.Errno))
        {
        }
    }

    private static bool ShouldIgnoreSpotOptionError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.ENotSup
               || code == ErrorCode.EInval
               || code == ErrorCode.EProtoNoSupport;
    }
}
