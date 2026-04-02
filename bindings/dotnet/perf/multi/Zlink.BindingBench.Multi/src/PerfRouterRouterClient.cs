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
        int pollTimeoutMs = ResolveEffectiveMultiClientPollTimeoutMs();
        ReadOnlySpan<byte> serverRoutingId = "SERVER"u8;

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new RouterSocket(ctx);
                ApplyMultiSocketOptions(client, pattern);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.SetOption(SocketOptions.RoutingId, $"CLIENT-{i}");
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<SocketBase> activeClients = WaitAllClientConnectReady(clients,
                monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            DisposeAllQuietly(monitors);
            monitors.Clear();

            var slots = CreateSlots(activeClients, serverRoutingId, size);
            var result = RunMultiRouterRouterClientLoop(slots, size,
                latencySampleCap, pollTimeoutMs,
                warmupSeconds, durationSeconds, settleMs, drainMs,
                sizeTransitionDrainMs, activeWarmup, warmupDrainMs,
                readyTimeoutMs);

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

    private static RouterRouterClientSlot[] CreateSlots(
        List<SocketBase> activeClients, ReadOnlySpan<byte> serverRoutingId,
        int msgSize)
    {
        var slots = new RouterRouterClientSlot[activeClients.Count];
        for (int i = 0; i < activeClients.Count; i++)
        {
            var payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
            Array.Fill(payload, (byte)'a');
            var routingId = new byte[256];
            var recv = new byte[Math.Max(256, Math.Max(msgSize, MultiStopToken.Length))];
            byte[] serverRoutingIdBytes = new byte[serverRoutingId.Length];
            serverRoutingId.CopyTo(serverRoutingIdBytes);
            slots[i] = new RouterRouterClientSlot(activeClients[i],
                serverRoutingIdBytes, payload, routingId, recv);
        }

        return slots;
    }

    private static (double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us)
        RunMultiRouterRouterClientLoop(RouterRouterClientSlot[] slots,
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
        var metrics = new RouterRouterMetrics(latSamples, latencySampleCap);

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
            TryScheduleIdleSends(slots, eventMasks, msgSize, runId, PerfPhase.Active,
                ref seq, ref rrIndex);

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

    private static void RunWarmupPhase(RouterRouterClientSlot[] slots,
        IReadOnlyList<SocketBase> sockets, PollEvents[] eventMasks, int msgSize,
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
                var ignoredMetrics = new RouterRouterMetrics(null, 0);
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics, allowSend: true);
            }
        }

        if (warmupDrainMs > 0)
            Thread.Sleep(warmupDrainMs);
    }

    private static void RunDrainPhase(RouterRouterClientSlot[] slots,
        IReadOnlyList<SocketBase> sockets, PollEvents[] eventMasks, int msgSize,
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
                var ignoredMetrics = new RouterRouterMetrics(null, 0);
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics, allowSend: false);
            }
        }
    }

    private static bool DrainPendingReplies(RouterRouterClientSlot[] slots,
        IReadOnlyList<SocketBase> sockets, PollEvents[] eventMasks, int msgSize,
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
                var ignoredMetrics = new RouterRouterMetrics(null, 0);
                HandleClientEvent(slots, i, eventMasks, msgSize, runId,
                    PerfPhase.Warmup, ref seq, ignoredMetrics, allowSend: false);
            }
        }

        return true;
    }

    private static bool HasPendingReplies(RouterRouterClientSlot[] slots)
    {
        for (int i = 0; i < slots.Length; i++)
        {
            if (slots[i].WaitingForReply)
                return true;
        }

        return false;
    }

    private static void TryScheduleIdleSends(RouterRouterClientSlot[] slots,
        PollEvents[] eventMasks, int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        ref int rrIndex)
    {
        int startIndex = rrIndex;
        for (int i = 0; i < slots.Length; i++)
        {
            int slotIndex = (startIndex + i) % slots.Length;
            RouterRouterClientSlot slot = slots[slotIndex];
            if (slot.WaitingForReply || slot.PendingStage != RouterSendStage.None)
                continue;

            PreparePayload(slot, msgSize, runId, phase, ref seq);
            if (TrySend(slot))
            {
                slot.WaitingForReply = true;
                UpdatePollMask(slot, eventMasks, slotIndex);
                continue;
            }

            UpdatePollMask(slot, eventMasks, slotIndex);
        }

        if (slots.Length > 0)
            rrIndex = (startIndex + 1) % slots.Length;
    }

    private static void HandleClientEvent(RouterRouterClientSlot[] slots,
        int slotIndex, PollEvents[] eventMasks,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        RouterRouterMetrics metrics, bool allowSend)
    {
        RouterRouterClientSlot slot = slots[slotIndex];

        if (IsSocketWriteReady(slotIndex)
            && slot.PendingStage != RouterSendStage.None
            && !slot.WaitingForReply)
        {
            if (TrySend(slot))
            {
                slot.WaitingForReply = true;
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
        }

        if (!IsSocketReadReady(slotIndex))
            return;

        while (TryReceiveReply(slot, out int received) && received > 0)
        {
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

            if (slot.PendingStage == RouterSendStage.None)
                PreparePayload(slot, msgSize, runId, phase, ref seq);
            if (TrySend(slot))
            {
                slot.WaitingForReply = true;
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
            else
            {
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
        }
    }

    private static List<SocketBase> CollectSockets(
        RouterRouterClientSlot[] slots)
    {
        var sockets = new List<SocketBase>(slots.Length);
        for (int i = 0; i < slots.Length; i++)
            sockets.Add(slots[i].Socket);
        return sockets;
    }

    private static void ResetPollMasks(RouterRouterClientSlot[] slots,
        PollEvents[] eventMasks)
    {
        for (int i = 0; i < slots.Length; i++)
            UpdatePollMask(slots[i], eventMasks, i);
    }

    private static void UpdatePollMask(RouterRouterClientSlot slot,
        PollEvents[] eventMasks, int index)
    {
        PollEvents events = SocketPollIn;
        if (slot.PendingStage != RouterSendStage.None && !slot.WaitingForReply)
            events |= SocketPollOut;
        eventMasks[index] = events;
    }

    private static void PreparePayload(RouterRouterClientSlot slot, int msgSize,
        uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq++, EpochUs());
        slot.PendingStage = RouterSendStage.RoutingId;
    }

    private static bool TrySend(RouterRouterClientSlot slot)
    {
        if (slot.PendingStage == RouterSendStage.RoutingId)
        {
            if (!slot.Socket.TrySend(slot.ServerRoutingId.AsSpan(),
                    SendFlags.SendMore | SendFlags.DontWait, out int ridWritten)
                || ridWritten <= 0)
            {
                return false;
            }
            slot.PendingStage = RouterSendStage.Payload;
        }

        if (slot.PendingStage == RouterSendStage.Payload)
        {
            if (!slot.Socket.TrySend(slot.Payload.AsSpan(), SendFlags.DontWait,
                    out int payloadWritten)
                || payloadWritten <= 0)
            {
                return false;
            }
            slot.PendingStage = RouterSendStage.None;
        }

        return true;
    }

    private static bool TryReceiveReply(RouterRouterClientSlot slot,
        out int payloadLength)
    {
        payloadLength = 0;
        int ridLen = ReceiveRetry(slot.Socket, slot.RoutingId.AsSpan(),
            ReceiveFlags.DontWait);
        if (ridLen <= 0)
            return false;

        int n = ReceiveRetry(slot.Socket, slot.Recv.AsSpan(),
            ReceiveFlags.DontWait);
        if (n <= 0)
            return false;

        payloadLength = n;
        return true;
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

    private static void TrySendRouterStopToken(
        IReadOnlyList<SocketBase> activeClients,
        ReadOnlySpan<byte> serverRoutingId)
    {
        if (activeClients == null || activeClients.Count == 0)
            return;

        for (int i = 0; i < activeClients.Count; i++)
        {
            try
            {
                if (!activeClients[i].TrySend(serverRoutingId,
                        SendFlags.SendMore | SendFlags.DontWait,
                        out int sentRid)
                    || sentRid <= 0)
                {
                    continue;
                }

                _ = activeClients[i].TrySend(MultiStopToken.AsSpan(),
                    SendFlags.DontWait, out _);
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

    private enum RouterSendStage
    {
        None = 0,
        RoutingId = 1,
        Payload = 2,
    }

    private sealed class RouterRouterClientSlot
    {
        internal RouterRouterClientSlot(SocketBase socket, byte[] serverRoutingId,
            byte[] payload, byte[] routingId, byte[] recv)
        {
            Socket = socket;
            ServerRoutingId = serverRoutingId;
            Payload = payload;
            RoutingId = routingId;
            Recv = recv;
        }

        internal SocketBase Socket { get; }
        internal byte[] ServerRoutingId { get; }
        internal byte[] Payload { get; }
        internal byte[] RoutingId { get; }
        internal byte[] Recv { get; }
        internal RouterSendStage PendingStage { get; set; }
        internal bool WaitingForReply { get; set; }
    }

    private sealed class RouterRouterMetrics
    {
        internal RouterRouterMetrics(List<double>? latencySamples,
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
