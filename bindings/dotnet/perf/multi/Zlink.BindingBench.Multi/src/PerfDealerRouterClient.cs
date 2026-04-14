using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerRouterClient
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int warmupSeconds = ResolveMultiWarmupSeconds(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        bool activeWarmup = ResolveMultiActiveWarmup(options);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        int pollTimeoutMs = Math.Max(0, options.ClientPollTimeoutMs);
        string endpoint = options.Endpoint;

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiClientContextOptions(ctx, options);
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new DealerSocket(ctx);
                ApplyMultiSocketOptions(client, options);
                ConfigureTlsClientIfNeeded(client, options.Transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.SetRoutingId(RoutingId.FromBytes(
                    System.Text.Encoding.ASCII.GetBytes($"CLIENT-{i}")));
                var monitor = client.MonitorOpen(SocketEvent.ConnectionReady);
                client.Connect(endpoint);
                clients.Add(client);
                monitors.Add(monitor);
            }

            List<SocketBase> activeClients = WaitAllClientConnectReady(
                pollManager, clients, monitors, readyTimeoutMs);
            if (activeClients.Count != clients.Count)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            DisposeAllQuietly(monitors);
            monitors.Clear();

            var slots = CreateSlots(activeClients, size);
            var result = RunMultiDealerRouterClientLoop(pollManager, slots,
                size, latencySampleCap, pollTimeoutMs, warmupSeconds,
                durationSeconds, activeWarmup, readyTimeoutMs);

            TrySendStopToken(activeClients);

            PrintResult(options.Pattern, options.Transport, size, result.throughput,
                result.latencyNs, result.latencyP95Ns, result.latencyP99Ns);
            return 0;
        }
        finally
        {
            DisposeAllQuietly(monitors);
            DisposeAllQuietly(clients);
        }
    }

    private static DealerRouterClientSlot[] CreateSlots(
        List<SocketBase> activeClients, int msgSize)
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

    private static (double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns)
        RunMultiDealerRouterClientLoop(PollManager pollManager,
            DealerRouterClientSlot[] slots, int msgSize, int latencySampleCap,
            int pollTimeoutMs, int warmupSeconds, int durationSeconds,
            bool activeWarmup, int readyTimeoutMs)
    {
        _ = warmupSeconds;
        _ = activeWarmup;
        _ = readyTimeoutMs;
        const uint runId = 1;
        var latSamples = new List<double>(latencySampleCap);
        ulong seq = 1;
        int rrIndex = 0;
        var metrics = new DealerRouterMetrics(latSamples, latencySampleCap);

        var sockets = CollectSockets(slots);
        var eventMasks = new PollEvents[slots.Length];
        ResetPollMasks(slots, eventMasks);

        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            TryScheduleIdleSends(slots, eventMasks, msgSize, runId,
                PerfPhase.Active, ref seq, ref rrIndex);

            if (PollSocketEvents(pollManager, sockets, eventMasks,
                    pollTimeoutMs) <= 0)
            {
                for (int i = 0; i < slots.Length; i++)
                    HandleClientEvent(pollManager, slots, i, eventMasks, msgSize,
                        runId, PerfPhase.Active, ref seq, metrics, allowSend: true);
                continue;
            }

            for (int i = 0; i < slots.Length; i++)
                HandleClientEvent(pollManager, slots, i, eventMasks, msgSize,
                    runId, PerfPhase.Active, ref seq, metrics, allowSend: true);
        }

        long benchEndTicks = Stopwatch.GetTimestamp();

        double elapsedSeconds = (benchEndTicks - benchStartTicks)
            / (double)Stopwatch.Frequency;
        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = metrics.MeasureCount / configuredSeconds;
        double fallbackLatencyNs = (configuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, metrics.MeasureCount * 2.0);
        var latency = ComputeLatencyStats(latSamples);
        double latencyNs = latency.mean > 0.0 ? latency.mean : fallbackLatencyNs;
        double latencyP95Ns = latency.p95 > 0.0 ? latency.p95 : latencyNs;
        double latencyP99Ns = latency.p99 > 0.0 ? latency.p99 : latencyP95Ns;

        return (throughput, latencyNs, latencyP95Ns, latencyP99Ns);
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

    private static void HandleClientEvent(PollManager pollManager,
        DealerRouterClientSlot[] slots,
        int slotIndex, PollEvents[] eventMasks,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        DealerRouterMetrics metrics, bool allowSend)
    {
        DealerRouterClientSlot slot = slots[slotIndex];

        bool readReady = IsSocketReadReady(pollManager, slotIndex);
        if (!readReady && !slot.WaitingForReply)
            return;

        while (true)
        {
            Received? receivedMessage = null;
            try
            {
                receivedMessage = ((MessageSocketBase)slot.Socket)
                    .Recv(RecvFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno))
            {
                break;
            }

            if (receivedMessage == null || receivedMessage.Parts.Count == 0)
                break;

            if (!slot.WaitingForReply)
            {
                DisposeReceived(receivedMessage);
                continue;
            }

            slot.WaitingForReply = false;
            if (phase == PerfPhase.Active)
            {
                ReadOnlySpan<byte> body = receivedMessage.Parts[0].AsReadOnlySpan();
                if (TryDecodeMetricHeader(body, out PerfMetricHeader header)
                    && header.RunId == runId
                    && header.MsgSize == (uint)msgSize
                    && header.Phase == (uint)phase)
                {
                    metrics.MeasureCount++;
                    if (metrics.LatencySamples != null && header.SentTsNs > 0)
                    {
                        ulong nowNs = EpochNs();
                        if (nowNs >= header.SentTsNs)
                        {
                            double sampleLatencyNs = nowNs - header.SentTsNs;
                            long sampleSeen = metrics.SampleSeen;
                            uint rng = metrics.Rng;
                            ReservoirSample(metrics.LatencySamples, sampleLatencyNs,
                                ref sampleSeen, metrics.LatencySampleCap, ref rng);
                            metrics.SampleSeen = sampleSeen;
                            metrics.Rng = rng;
                        }
                    }
                }
            }

            if (!allowSend)
            {
                DisposeReceived(receivedMessage);
                continue;
            }

            slot.WaitingForWritable = false;
            UpdatePollMask(slot, eventMasks, slotIndex);
            DisposeReceived(receivedMessage);
        }
    }

    private static List<SocketBase> CollectSockets(
        DealerRouterClientSlot[] slots)
    {
        var sockets = new List<SocketBase>(slots.Length);
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
            seq++, EpochNs());
    }

    private static bool TrySend(DealerRouterClientSlot slot)
    {
        try
        {
            using Message message = Message.FromBytes(slot.Payload);
            ((MessageSocketBase)slot.Socket).Send(message);
            return true;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                        || IsInterrupted(ex.InternalErrno))
        {
            return false;
        }
    }

    private static void DisposeReceived(Received? received)
    {
        if (received == null)
            return;

        for (int i = 0; i < received.Parts.Count; i++)
            TryDisposeQuietly(received.Parts[i]);
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
        internal DealerRouterClientSlot(SocketBase socket, byte[] payload,
            byte[] recv)
        {
            Socket = socket;
            Payload = payload;
            Recv = recv;
        }

        internal SocketBase Socket { get; }
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
