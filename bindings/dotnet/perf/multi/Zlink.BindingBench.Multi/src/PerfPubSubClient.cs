using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSubClient
{
    internal static int Run(string transport, int size,
        string endpoint)
    {
        const string pattern = "PUBSUB";
        const int subscribeSettleMs = 300;
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        if (size >= 65536 && warmupSeconds > 0)
            warmupSeconds = 0;
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

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var clients = new List<Zlink.Socket>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new Zlink.Socket(ctx, Zlink.SocketType.Sub);
                ApplyMultiSocketOptions(client, pattern);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.SetOption(SocketOptions.Subscribe, string.Empty);
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

            var recv = new byte[Math.Max(256, Math.Max(size, MultiStopToken.Length))];

            Thread.Sleep(subscribeSettleMs);
            var result = RunMultiPubSubClientLoop(activeClients, recv,
                size, latencySampleCap,
                warmupSeconds, durationSeconds, settleMs, drainMs,
                sizeTransitionDrainMs, activeWarmup, warmupDrainMs);

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
        RunMultiPubSubClientLoop(List<Zlink.Socket> activeClients,
            byte[] recv, int msgSize, int latencySampleCap, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs)
    {
        const uint expectedRunId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        var events = new List<PollEvent>(activeClients.Count);
        bool activeStarted = false;
        long measureCount = 0;
        long loopStartTicks = Stopwatch.GetTimestamp();
        long benchStartTicks = 0;
        long benchEndTicks = 0;
        long benchDeadlineTicks = 0;
        long waitActiveDeadlineTicks = loopStartTicks
            + (long)Math.Max(6, durationSeconds + warmupSeconds + 2)
            * Stopwatch.Frequency;

        using var poller = new Poller();
        for (int i = 0; i < activeClients.Count; i++)
            poller.Add(activeClients[i], PollEvents.PollIn);

        _ = activeWarmup;
        if (warmupSeconds > 0)
        {
            long warmupDeadlineTicks = Stopwatch.GetTimestamp()
                + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
            {
                int remainingMs = RemainingMilliseconds(warmupDeadlineTicks);
                if (!WaitForEvents(poller, events, remainingMs))
                    continue;

                for (int i = 0; i < events.Count; i++)
                {
                    Zlink.Socket? socket = events[i].Socket;
                    if (socket == null)
                        continue;
                    DrainReadableSocket(socket, recv.AsSpan(),
                        static _ => true);
                }
            }

            if (warmupDrainMs > 0)
                Thread.Sleep(warmupDrainMs);
        }

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (activeStarted)
            {
                if (nowTicks >= benchDeadlineTicks)
                    break;
            }
            else if (nowTicks >= waitActiveDeadlineTicks)
            {
                break;
            }

            if (!WaitForEvents(poller, events,
                    activeStarted ? RemainingMilliseconds(benchDeadlineTicks)
                        : RemainingMilliseconds(waitActiveDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < events.Count; i++)
            {
                Zlink.Socket? socket = events[i].Socket;
                if (socket == null)
                    continue;

                DrainReadableSocket(socket, recv.AsSpan(), body =>
                {
                    long endTicks = Stopwatch.GetTimestamp();
                    bool headerOk = TryDecodeMetricHeader(body,
                        out PerfMetricHeader header);
                    if (!headerOk
                        || header.RunId != expectedRunId
                        || header.MsgSize != (uint)msgSize)
                    {
                        return true;
                    }

                    if (header.Phase == (uint)PerfPhase.Active)
                    {
                        if (!activeStarted)
                        {
                            activeStarted = true;
                            benchStartTicks = endTicks;
                            benchDeadlineTicks = benchStartTicks
                                + (long)Math.Max(1, durationSeconds)
                                * Stopwatch.Frequency;
                        }

                        if (endTicks <= benchDeadlineTicks)
                        {
                            measureCount++;
                            benchEndTicks = endTicks;
                            ulong nowUs = EpochUs();
                            if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                            {
                                double sampleLatencyUs = nowUs - header.SentTsUs;
                                ReservoirSample(latSamples, sampleLatencyUs,
                                    ref sampleSeen, latencySampleCap, ref rng);
                            }
                        }
                    }
                    return true;
                });
            }
        }

        long loopEndTicks = Stopwatch.GetTimestamp();
        if (benchStartTicks == 0)
            benchStartTicks = loopStartTicks;
        if (benchEndTicks <= benchStartTicks)
            benchEndTicks = loopEndTicks;

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
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(latSamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;

        return (throughput, latencyUs, latencyP95Us, latencyP99Us);
    }

    private static int RemainingMilliseconds(long deadlineTicks)
    {
        long nowTicks = Stopwatch.GetTimestamp();
        if (deadlineTicks <= nowTicks)
            return 0;

        double remainingMs = (deadlineTicks - nowTicks) * 1000.0
            / Stopwatch.Frequency;
        if (remainingMs >= int.MaxValue)
            return int.MaxValue;
        return (int)Math.Ceiling(remainingMs);
    }
}
