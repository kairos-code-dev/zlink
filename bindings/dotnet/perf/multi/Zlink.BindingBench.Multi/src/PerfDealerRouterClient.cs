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
        int pollTimeoutMs = ResolveEffectiveMultiClientPollTimeoutMs();

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
                latencySampleCap, pollTimeoutMs, warmupSeconds,
                durationSeconds, settleMs, drainMs, sizeTransitionDrainMs,
                activeWarmup, warmupDrainMs, readyTimeoutMs);

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
            int msgSize, int latencySampleCap, int pollTimeoutMs,
            int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs,
            int readyTimeoutMs)
    {
        const uint runId = 1;
        var latSamples = new List<double>(latencySampleCap);
        ulong seq = 1;
        int rrIndex = 0;
        var metrics = new DealerRouterMetrics(latSamples, latencySampleCap);

        var sockets = CollectSockets(slots);
        var eventMasks = new PollEvents[slots.Length];
        ResetPollMasks(slots, eventMasks);

        RunWarmupPhase(slots, sockets, eventMasks, msgSize, warmupSeconds,
            activeWarmup, warmupDrainMs, runId, ref seq, ref rrIndex,
            pollTimeoutMs);

        RunDrainPhase(slots, sockets, eventMasks, msgSize, settleMs, runId,
            ref seq, pollTimeoutMs);
        if (!DrainPendingReplies(slots, sockets, eventMasks, msgSize, runId,
                readyTimeoutMs, settleMs, pollTimeoutMs,
                ref seq))
        {
            return (0.0, 0.0, 0.0, 0.0);
        }

        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            TryScheduleIdleSends(slots, eventMasks, msgSize, runId,
                PerfPhase.Active, ref seq, ref rrIndex);

            if (PollSocketEvents(sockets, eventMasks, pollTimeoutMs) <= 0)
                continue;

            for (int i = 0; i < slots.Length; i++)
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Active, ref seq, metrics, allowSend: true);
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
        IReadOnlyList<Zlink.Socket> sockets, PollEvents[] eventMasks, int msgSize,
        int warmupSeconds,
        bool activeWarmup, int warmupDrainMs, uint runId, ref ulong seq,
        ref int rrIndex, int pollTimeoutMs)
    {
        _ = activeWarmup;
        if (warmupSeconds <= 0)
            return;

        long warmupDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
        {
            TryScheduleIdleSends(slots, eventMasks, msgSize, runId,
                PerfPhase.Warmup,
                ref seq, ref rrIndex);

            if (PollSocketEvents(sockets, eventMasks, pollTimeoutMs) <= 0)
                continue;

            for (int i = 0; i < slots.Length; i++)
            {
                var ignoredMetrics = new DealerRouterMetrics(null, 0);
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics, allowSend: true);
            }
        }

        if (warmupDrainMs > 0)
            Thread.Sleep(warmupDrainMs);
    }

    private static void RunDrainPhase(DealerRouterClientSlot[] slots,
        IReadOnlyList<Zlink.Socket> sockets, PollEvents[] eventMasks, int msgSize,
        int settleMs,
        uint runId, ref ulong seq, int pollTimeoutMs)
    {
        if (settleMs <= 0)
            return;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, settleMs) * Stopwatch.Frequency / 1000;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (PollSocketEvents(sockets, eventMasks, pollTimeoutMs) <= 0)
                continue;

            for (int i = 0; i < slots.Length; i++)
            {
                var ignoredMetrics = new DealerRouterMetrics(null, 0);
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics, allowSend: false);
            }
        }
    }

    private static bool DrainPendingReplies(DealerRouterClientSlot[] slots,
        IReadOnlyList<Zlink.Socket> sockets, PollEvents[] eventMasks, int msgSize,
        uint runId,
        int readyTimeoutMs, int settleMs, int pollTimeoutMs, ref ulong seq)
    {
        if (!HasPendingReplies(slots))
            return true;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(readyTimeoutMs, Math.Max(100, settleMs))
            * Stopwatch.Frequency / 1000;
        while (HasPendingReplies(slots))
        {
            int timeoutMs = Math.Min(pollTimeoutMs,
                RemainingMilliseconds(deadlineTicks));
            if (timeoutMs <= 0)
                return false;
            if (PollSocketEvents(sockets, eventMasks, timeoutMs) <= 0)
                continue;

            for (int i = 0; i < slots.Length; i++)
            {
                var ignoredMetrics = new DealerRouterMetrics(null, 0);
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics, allowSend: false);
            }
        }

        return true;
    }

    private static bool HasPendingReplies(DealerRouterClientSlot[] slots)
    {
        for (int i = 0; i < slots.Length; i++)
        {
            if (slots[i].WaitingForReply)
                return true;
        }

        return false;
    }

    private static void TryScheduleIdleSends(DealerRouterClientSlot[] slots,
        PollEvents[] eventMasks, int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        ref int rrIndex)
    {
        int startIndex = rrIndex;
        for (int i = 0; i < slots.Length; i++)
        {
            int slotIndex = (startIndex + i) % slots.Length;
            DealerRouterClientSlot slot = slots[slotIndex];
            if (slot.WaitingForReply || slot.WaitingForWritable)
                continue;

            PreparePayload(slot, msgSize, runId, phase, ref seq);
            if (TrySend(slot))
            {
                slot.WaitingForReply = true;
                slot.WaitingForWritable = false;
                UpdatePollMask(slot, eventMasks, slotIndex);
                continue;
            }

            slot.WaitingForWritable = true;
            UpdatePollMask(slot, eventMasks, slotIndex);
        }

        if (slots.Length > 0)
            rrIndex = (startIndex + 1) % slots.Length;
    }

    private static void HandleClientEvent(DealerRouterClientSlot[] slots,
        int slotIndex, PollEvents[] eventMasks,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        DealerRouterMetrics metrics, bool allowSend)
    {
        DealerRouterClientSlot slot = slots[slotIndex];

        if (IsSocketWriteReady(slotIndex)
            && slot.WaitingForWritable
            && !slot.WaitingForReply)
        {
            if (TrySend(slot))
            {
                slot.WaitingForWritable = false;
                slot.WaitingForReply = true;
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
        }

        if (!IsSocketReadReady(slotIndex))
            return;

        while (true)
        {
            int received = TryReceiveNonBlocking(slot.Socket, slot.Recv.AsSpan());
            if (received <= 0)
                break;

            if (!slot.WaitingForReply)
                continue;

            slot.WaitingForReply = false;
            if (phase == PerfPhase.Active)
            {
                ReadOnlySpan<byte> body = slot.Recv.AsSpan(0, received);
                if (TryDecodeMetricHeader(body, out PerfMetricHeader header)
                    && header.RunId == runId
                    && header.MsgSize == (uint)msgSize
                    && header.Phase == (uint)phase)
                {
                    metrics.MeasureCount++;
                    if (metrics.LatencySamples != null && header.SentTsUs > 0)
                    {
                        ulong nowUs = EpochUs();
                        if (nowUs >= header.SentTsUs)
                        {
                            double sampleLatencyUs = nowUs - header.SentTsUs;
                            long sampleSeen = metrics.SampleSeen;
                            uint rng = metrics.Rng;
                            ReservoirSample(metrics.LatencySamples, sampleLatencyUs,
                                ref sampleSeen, metrics.LatencySampleCap, ref rng);
                            metrics.SampleSeen = sampleSeen;
                            metrics.Rng = rng;
                        }
                    }
                }
            }

            if (!allowSend)
                continue;

            if (!slot.WaitingForWritable)
                PreparePayload(slot, msgSize, runId, phase, ref seq);

            if (TrySend(slot))
            {
                slot.WaitingForReply = true;
                slot.WaitingForWritable = false;
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
            else if (!slot.WaitingForWritable)
            {
                slot.WaitingForWritable = true;
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
        }
    }

    private static List<Zlink.Socket> CollectSockets(
        DealerRouterClientSlot[] slots)
    {
        var sockets = new List<Zlink.Socket>(slots.Length);
        for (int i = 0; i < slots.Length; i++)
            sockets.Add(slots[i].Socket);
        return sockets;
    }

    private static void ResetPollMasks(DealerRouterClientSlot[] slots,
        PollEvents[] eventMasks)
    {
        for (int i = 0; i < slots.Length; i++)
            UpdatePollMask(slots[i], eventMasks, i);
    }

    private static void UpdatePollMask(DealerRouterClientSlot slot,
        PollEvents[] eventMasks, int index)
    {
        PollEvents events = SocketPollIn;
        if (slot.WaitingForWritable)
            events |= SocketPollOut;
        eventMasks[index] = events;
    }

    private static void PreparePayload(DealerRouterClientSlot slot, int msgSize,
        uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq++, EpochUs());
    }

    private static bool TrySend(DealerRouterClientSlot slot)
    {
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
