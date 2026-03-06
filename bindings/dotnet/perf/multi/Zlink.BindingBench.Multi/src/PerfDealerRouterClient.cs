using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerRouterClient
{
    internal static int Run(string transport, int size, string endpoint)
    {
        const string pattern = "DEALER_ROUTER";
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

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var clients = new List<Zlink.Socket>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new Zlink.Socket(ctx, Zlink.SocketType.Dealer);
                ApplyMultiSocketOptions(client, pattern);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
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

            var slots = CreateSlots(activeClients, size);
            var result = RunMultiDealerRouterClientLoop(slots, size,
                latencySampleCap, warmupSeconds, durationSeconds, settleMs,
                drainMs, sizeTransitionDrainMs, activeWarmup, warmupDrainMs);

            TrySendStopToken(activeClients);

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

    private static DealerRouterClientSlot[] CreateSlots(
        List<Zlink.Socket> activeClients, int msgSize)
    {
        var slots = new DealerRouterClientSlot[activeClients.Count];
        for (int i = 0; i < activeClients.Count; i++)
        {
            var payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
            Array.Fill(payload, (byte)'a');
            var recv = new byte[Math.Max(256, Math.Max(msgSize, MultiStopToken.Length))];
            slots[i] = new DealerRouterClientSlot(activeClients[i], payload, recv);
        }

        return slots;
    }

    private static (double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us)
        RunMultiDealerRouterClientLoop(DealerRouterClientSlot[] slots,
            int msgSize, int latencySampleCap, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs)
    {
        const uint runId = 1;
        var latSamples = new List<double>(latencySampleCap);
        ulong seq = 1;
        var metrics = new DealerRouterMetrics(latSamples, latencySampleCap);

        using var poller = new Poller();
        var pollEvents = new List<PollEvent>(slots.Length);
        for (int i = 0; i < slots.Length; i++)
            poller.Add(slots[i].Socket, PollEvents.PollIn, slots[i]);

        RunWarmupPhase(slots, poller, pollEvents, msgSize, warmupSeconds,
            activeWarmup, warmupDrainMs, runId, ref seq);

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            TryScheduleIdleSends(slots, poller, msgSize, runId, PerfPhase.Active,
                ref seq);

            if (!WaitForEvents(poller, pollEvents,
                    RemainingMilliseconds(benchDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < pollEvents.Count; i++)
                HandleClientEvent(pollEvents[i], poller, msgSize, runId,
                    PerfPhase.Active, ref seq, metrics);
        }

        long benchEndTicks = Stopwatch.GetTimestamp();
        if (drainMs > 0)
            Thread.Sleep(drainMs);
        if (sizeTransitionDrainMs > 0)
            Thread.Sleep(sizeTransitionDrainMs);

        double elapsedSeconds = (benchEndTicks - benchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? metrics.MeasureCount / elapsedSeconds
            : 0.0;
        double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, metrics.MeasureCount * 2.0);
        var latency = ComputeLatencyStats(latSamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;

        return (throughput, latencyUs, latencyP95Us, latencyP99Us);
    }

    private static void RunWarmupPhase(DealerRouterClientSlot[] slots,
        Poller poller, List<PollEvent> pollEvents, int msgSize, int warmupSeconds,
        bool activeWarmup, int warmupDrainMs, uint runId, ref ulong seq)
    {
        if (!activeWarmup || warmupSeconds <= 0)
            return;

        long warmupDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
        {
            TryScheduleIdleSends(slots, poller, msgSize, runId, PerfPhase.Warmup,
                ref seq);

            if (!WaitForEvents(poller, pollEvents,
                    RemainingMilliseconds(warmupDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < pollEvents.Count; i++)
            {
                var ignoredMetrics = new DealerRouterMetrics(null, 0);
                HandleClientEvent(pollEvents[i], poller, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics);
            }
        }

        if (warmupDrainMs > 0)
            Thread.Sleep(warmupDrainMs);
    }

    private static void TryScheduleIdleSends(DealerRouterClientSlot[] slots,
        Poller poller, int msgSize, uint runId, PerfPhase phase, ref ulong seq)
    {
        for (int i = 0; i < slots.Length; i++)
        {
            DealerRouterClientSlot slot = slots[i];
            if (slot.WaitingForReply || slot.WaitingForWritable)
                continue;

            if (TrySend(slot, msgSize, runId, phase, ref seq))
            {
                slot.WaitingForReply = true;
                slot.SendStartTicks = Stopwatch.GetTimestamp();
                continue;
            }

            slot.WaitingForWritable = true;
            poller.Modify(slot.Socket, PollEvents.PollIn | PollEvents.PollOut);
        }
    }

    private static void HandleClientEvent(PollEvent pollEvent, Poller poller,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        DealerRouterMetrics metrics)
    {
        if (pollEvent.Tag is not DealerRouterClientSlot slot)
            return;

        if ((pollEvent.Revents & PollEvents.PollOut) != 0
            && slot.WaitingForWritable
            && !slot.WaitingForReply)
        {
            if (TrySend(slot, msgSize, runId, phase, ref seq))
            {
                slot.WaitingForWritable = false;
                slot.WaitingForReply = true;
                slot.SendStartTicks = Stopwatch.GetTimestamp();
                poller.Modify(slot.Socket, PollEvents.PollIn);
            }
        }

        if ((pollEvent.Revents & PollEvents.PollIn) == 0)
            return;

        DrainReadableSocket(slot.Socket, slot.Recv.AsSpan(), body =>
        {
            if (!slot.WaitingForReply)
                return true;

            slot.WaitingForReply = false;
            if (phase != PerfPhase.Active)
                return true;

            long endTicks = Stopwatch.GetTimestamp();
            metrics.MeasureCount++;
            if (metrics.LatencySamples != null)
            {
                double oneWayLatencyUs = ((endTicks - slot.SendStartTicks)
                                          * 1_000_000.0 / Stopwatch.Frequency)
                    / 2.0;
                long sampleSeen = metrics.SampleSeen;
                uint rng = metrics.Rng;
                ReservoirSample(metrics.LatencySamples, oneWayLatencyUs,
                    ref sampleSeen, metrics.LatencySampleCap, ref rng);
                metrics.SampleSeen = sampleSeen;
                metrics.Rng = rng;
            }
            return true;
        });
    }

    private static bool TrySend(DealerRouterClientSlot slot, int msgSize,
        uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq++, EpochUs());
        try
        {
            return slot.Socket.Send(slot.Payload.AsSpan(), SendFlags.DontWait) > 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            return false;
        }
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

    private sealed class DealerRouterClientSlot
    {
        internal DealerRouterClientSlot(Zlink.Socket socket, byte[] payload,
            byte[] recv)
        {
            Socket = socket;
            Payload = payload;
            Recv = recv;
        }

        internal Zlink.Socket Socket { get; }
        internal byte[] Payload { get; }
        internal byte[] Recv { get; }
        internal bool WaitingForReply { get; set; }
        internal bool WaitingForWritable { get; set; }
        internal long SendStartTicks { get; set; }
    }

    private sealed class DealerRouterMetrics
    {
        internal DealerRouterMetrics(List<double>? latencySamples,
            int latencySampleCap)
        {
            LatencySamples = latencySamples;
            LatencySampleCap = latencySampleCap;
            SampleSeen = 0;
            Rng = 0xA341316Cu;
        }

        internal long MeasureCount { get; set; }
        internal List<double>? LatencySamples { get; }
        internal int LatencySampleCap { get; }
        internal long SampleSeen { get; set; }
        internal uint Rng { get; set; }
    }
}
