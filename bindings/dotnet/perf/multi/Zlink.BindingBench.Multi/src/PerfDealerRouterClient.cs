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
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<Zlink.Socket> activeClients = WaitAllClientConnectReady(clients,
                monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            DisposeAllQuietly(monitors);
            monitors.Clear();

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
        int rrIndex = 0;
        var metrics = new DealerRouterMetrics(latSamples, latencySampleCap);

        using var poller = new Poller();
        var pollEvents = new List<PollEvent>(slots.Length);
        for (int i = 0; i < slots.Length; i++)
            poller.Add(slots[i].Socket, PollEvents.PollIn, slots[i]);

        RunWarmupPhase(slots, poller, pollEvents, msgSize, warmupSeconds,
            activeWarmup, warmupDrainMs, runId, ref seq, ref rrIndex);

        RunDrainPhase(slots, poller, pollEvents, msgSize, settleMs, runId,
            ref seq);

        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            TryScheduleIdleSends(slots, poller, msgSize, runId, PerfPhase.Active,
                ref seq, ref rrIndex);

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
        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = metrics.MeasureCount / configuredSeconds;
        double fallbackLatencyUs = (configuredSeconds * 1_000_000.0)
            / Math.Max(1.0, metrics.MeasureCount * 2.0);
        var latency = ComputeLatencyStats(latSamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;

        return (throughput, latencyUs, latencyP95Us, latencyP99Us);
    }

    private static void RunWarmupPhase(DealerRouterClientSlot[] slots,
        Poller poller, List<PollEvent> pollEvents, int msgSize, int warmupSeconds,
        bool activeWarmup, int warmupDrainMs, uint runId, ref ulong seq,
        ref int rrIndex)
    {
        _ = activeWarmup;
        if (warmupSeconds <= 0)
            return;

        long warmupDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
        {
            TryScheduleIdleSends(slots, poller, msgSize, runId, PerfPhase.Warmup,
                ref seq, ref rrIndex);

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

    private static void RunDrainPhase(DealerRouterClientSlot[] slots,
        Poller poller, List<PollEvent> pollEvents, int msgSize, int settleMs,
        uint runId, ref ulong seq)
    {
        if (settleMs <= 0)
            return;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, settleMs) * Stopwatch.Frequency / 1000;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (!WaitForEvents(poller, pollEvents,
                    RemainingMilliseconds(deadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < pollEvents.Count; i++)
            {
                var ignoredMetrics = new DealerRouterMetrics(null, 0);
                HandleClientEvent(pollEvents[i], poller, msgSize, runId,
                    PerfPhase.Drain, ref seq, ignoredMetrics);
            }
        }
    }

    private static void TryScheduleIdleSends(DealerRouterClientSlot[] slots,
        Poller poller, int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        ref int rrIndex)
    {
        int startIndex = rrIndex;
        for (int i = 0; i < slots.Length; i++)
        {
            int slotIndex = (startIndex + i) % slots.Length;
            DealerRouterClientSlot slot = slots[slotIndex];
            if (slot.WaitingForReply || slot.WaitingForWritable)
                continue;

            if (TrySend(slot, msgSize, runId, phase, ref seq))
            {
                slot.WaitingForReply = true;
                continue;
            }

            slot.WaitingForWritable = true;
            poller.Modify(slot.Socket, PollEvents.PollIn | PollEvents.PollOut);
        }

        if (slots.Length > 0)
            rrIndex = (startIndex + 1) % slots.Length;
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
                poller.Modify(slot.Socket, PollEvents.PollIn);
            }
        }

        if ((pollEvent.Revents & PollEvents.PollIn) == 0)
            return;

        while (true)
        {
            int received = TryReceiveNonBlocking(slot.Socket, slot.Recv.AsSpan());
            if (received <= 0)
                break;

            if (!slot.WaitingForReply)
                continue;

            slot.WaitingForReply = false;
            if (phase != PerfPhase.Active)
                continue;

            ReadOnlySpan<byte> body = slot.Recv.AsSpan(0, received);
            if (!TryDecodeMetricHeader(body, out PerfMetricHeader header)
                || header.RunId != runId
                || header.MsgSize != (uint)msgSize
                || header.Phase != (uint)phase)
            {
                continue;
            }

            metrics.MeasureCount++;
            if (metrics.LatencySamples != null && header.SentTsUs > 0)
            {
                ulong nowUs = EpochUs();
                if (nowUs < header.SentTsUs)
                    continue;

                double oneWayLatencyUs = (nowUs - header.SentTsUs) / 2.0;
                long sampleSeen = metrics.SampleSeen;
                uint rng = metrics.Rng;
                ReservoirSample(metrics.LatencySamples, oneWayLatencyUs,
                    ref sampleSeen, metrics.LatencySampleCap, ref rng);
                metrics.SampleSeen = sampleSeen;
                metrics.Rng = rng;
            }
        }
    }

    private static bool TrySend(DealerRouterClientSlot slot, int msgSize,
        uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq++, EpochUs());
        return slot.Socket.TrySend(slot.Payload.AsSpan(), SendFlags.DontWait,
            out int written) && written > 0;
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
