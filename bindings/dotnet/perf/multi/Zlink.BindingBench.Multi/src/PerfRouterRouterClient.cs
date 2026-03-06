using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouterClient
{
    internal static int Run(string transport, int size,
        string endpoint)
    {
        const string pattern = "ROUTER_ROUTER";
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int drainMs = ResolveMultiDrainMs(pattern);
        int sizeTransitionDrainMs = ResolveMultiSizeTransitionDrainMs();
        bool activeWarmup = ResolveMultiActiveWarmup();
        int warmupDrainMs = ResolveMultiWarmupDrainMs(drainMs);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
        int latencySampleCap = ResolveMultiLatencySampleCap();
        int clientCount = ResolveMultiClients(pattern);
        ReadOnlySpan<byte> serverRoutingId = "SERVER"u8;

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var clients = new List<Zlink.Socket>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new Zlink.Socket(ctx, Zlink.SocketType.Router);
                ApplyMultiSocketOptions(client, pattern);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.SetOption(SocketOptions.RoutingId, $"CLIENT-{i}");
                var monitor = client.MonitorOpen(
                    SocketEvent.ConnectionReady | SocketEvent.Connected);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<Zlink.Socket> activeClients = WaitAllClientConnectReady(clients,
                monitors, readyTimeoutMs);
            if (activeClients.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            var payload = new byte[Math.Max(size, PerfMetricHeaderSize)];
            Array.Fill(payload, (byte)'a');
            var routingId = new byte[256];
            var recv = new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];
            var result = RunMultiRouterRouterClientLoop(activeClients,
                serverRoutingId, payload, routingId, recv, size,
                latencySampleCap,
                warmupSeconds, durationSeconds, settleMs, drainMs,
                sizeTransitionDrainMs, activeWarmup, warmupDrainMs);

            TrySendRouterStopToken(activeClients, serverRoutingId);

            PrintResult(pattern, transport, size, result.throughput,
                result.latencyUs, result.latencyP95Us, result.latencyP99Us);
            return 0;
        }
        finally
        {
            DisposeAllQuietly(monitors);
            DisposeAllQuietly(clients);
        }
    }

    private static (double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us)
        RunMultiRouterRouterClientLoop(List<Zlink.Socket> activeClients,
            ReadOnlySpan<byte> serverRoutingId, byte[] payload, byte[] routingId,
            byte[] recv, int msgSize, int latencySampleCap, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs)
    {
        const uint runId = 1;
        int index = 0;
        int clientCount = activeClients.Count;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        ulong seq = 1;

        if (activeWarmup)
        {
            long warmupDeadlineTicks = Stopwatch.GetTimestamp()
                + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
            {
                Zlink.Socket client = activeClients[index];
                StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Warmup,
                    msgSize, seq++, EpochUs());
                int sentRid = SendBlocking(client, serverRoutingId,
                    SendFlags.SendMore);
                int sentPayload = sentRid > 0
                    ? SendBlocking(client, payload.AsSpan(), SendFlags.None)
                    : 0;
                if (sentPayload > 0)
                {
                    int ridLen = ReceiveRetry(client, routingId.AsSpan(),
                        ReceiveFlags.None);
                    if (ridLen > 0)
                        _ = ReceiveRetry(client, recv.AsSpan(), ReceiveFlags.None);
                }
                index++;
                if (index == clientCount)
                    index = 0;
            }

            if (warmupDrainMs > 0)
                Thread.Sleep(warmupDrainMs);
        }
        else if (warmupSeconds > 0)
        {
            Thread.Sleep(warmupSeconds * 1000);
        }

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        long measureCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            Zlink.Socket client = activeClients[index];
            long beginTicks = Stopwatch.GetTimestamp();
            StampMetricHeader(payload.AsSpan(), runId, PerfPhase.Active, msgSize,
                seq++, EpochUs());
            int sentRid = SendBlocking(client, serverRoutingId, SendFlags.SendMore);
            if (sentRid <= 0)
            {
                index++;
                if (index == clientCount)
                    index = 0;
                continue;
            }
            int sentPayload = SendBlocking(client, payload.AsSpan(), SendFlags.None);
            if (sentPayload <= 0)
            {
                index++;
                if (index == clientCount)
                    index = 0;
                continue;
            }
            int ridLen = ReceiveRetry(client, routingId.AsSpan(),
                ReceiveFlags.None);
            if (ridLen <= 0)
            {
                index++;
                if (index == clientCount)
                    index = 0;
                continue;
            }
            int received = ReceiveRetry(client, recv.AsSpan(), ReceiveFlags.None);
            if (received <= 0)
            {
                index++;
                if (index == clientCount)
                    index = 0;
                continue;
            }
            long endTicks = Stopwatch.GetTimestamp();
            measureCount++;
            double oneWayLatencyUs = ((endTicks - beginTicks) * 1_000_000.0
                                      / Stopwatch.Frequency) / 2.0;
            ReservoirSample(latSamples, oneWayLatencyUs, ref sampleSeen,
                latencySampleCap, ref rng);
            index++;
            if (index == clientCount)
                index = 0;
        }
        long benchEndTicks = Stopwatch.GetTimestamp();

        if (drainMs > 0)
            Thread.Sleep(drainMs);
        if (sizeTransitionDrainMs > 0)
            Thread.Sleep(sizeTransitionDrainMs);

        double elapsedSeconds = (benchEndTicks - benchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? measureCount / elapsedSeconds
            : 0.0;
        double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, measureCount * 2.0);
        var latency = ComputeLatencyStats(latSamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;

        return (throughput, latencyUs, latencyP95Us, latencyP99Us);
    }

    private static void TrySendRouterStopToken(
        IReadOnlyList<Zlink.Socket> activeClients,
        ReadOnlySpan<byte> serverRoutingId)
    {
        if (activeClients == null || activeClients.Count == 0)
            return;

        for (int i = 0; i < activeClients.Count; i++)
        {
            try
            {
                int sentRid = SendBlocking(activeClients[i], serverRoutingId,
                    SendFlags.SendMore);
                if (sentRid <= 0)
                    continue;
                _ = SendBlocking(activeClients[i], MultiStopToken.AsSpan(),
                    SendFlags.None);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno)
                                            || IsTransientNetworkError(ex.Errno))
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }
}
