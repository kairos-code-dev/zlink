using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfMultiRouterRouterClient
{
    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int latencySampleCap = ResolveMultiLatencySampleCap(options);
        int clientCount = ResolveMultiClients(options);
        const int pollTimeoutMs = 50;
        string endpoint = options.Endpoint;
        ReadOnlySpan<byte> serverRoutingId = "SERVER"u8;

        using var ctx = new Context();
        using var pollManager = new PollManager();
        ApplyMultiClientContextOptions(ctx, options);
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new RouterSocket(ctx);
                ApplyMultiSocketOptions(client, options);
                ConfigureTlsClientIfNeeded(client, options.Transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
                client.RouterOptions.RoutingId = RoutingId.FromBytes(
                    System.Text.Encoding.ASCII.GetBytes($"CLIENT-{i}"));
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

            var slots = CreateSlots(activeClients, serverRoutingId, size);
            var result = RunMultiRouterRouterClientLoop(pollManager, slots,
                size, latencySampleCap, pollTimeoutMs, durationSeconds,
                readyTimeoutMs);

            TrySendRouterStopToken(activeClients, serverRoutingId);

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

    private static RouterRouterClientSlot[] CreateSlots(
        List<SocketBase> activeClients, ReadOnlySpan<byte> serverRoutingId,
        int msgSize)
    {
        var slots = new RouterRouterClientSlot[activeClients.Count];
        for (int i = 0; i < activeClients.Count; i++)
        {
            var payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
            Array.Fill(payload, (byte)'a');
            slots[i] = new RouterRouterClientSlot(activeClients[i],
                RoutingId.FromBytes(serverRoutingId), payload);
        }

        return slots;
    }

    private static (double throughput, double latencyNs, double latencyP95Ns,
        double latencyP99Ns)
        RunMultiRouterRouterClientLoop(PollManager pollManager,
            RouterRouterClientSlot[] slots, int msgSize, int latencySampleCap,
            int pollTimeoutMs, int durationSeconds, int readyTimeoutMs)
    {
        _ = readyTimeoutMs;
        const uint runId = 1;
        var latSamples = new List<double>(latencySampleCap);
        ulong seq = 1;
        int rrIndex = 0;
        var metrics = new RouterRouterMetrics(latSamples, latencySampleCap);

        var sockets = CollectSockets(slots);
        var eventMasks = new PollEvents[slots.Length];
        ResetPollMasks(slots, eventMasks);

        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            TryScheduleIdleSends(slots, eventMasks, msgSize, runId, PerfPhase.Active,
                ref seq, ref rrIndex);

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

    private static void TryScheduleIdleSends(RouterRouterClientSlot[] slots,
        PollEvents[] eventMasks, int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        ref int rrIndex)
    {
        int startIndex = rrIndex;
        for (int i = 0; i < slots.Length; i++)
        {
            int slotIndex = (startIndex + i) % slots.Length;
            RouterRouterClientSlot slot = slots[slotIndex];
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
        RouterRouterClientSlot[] slots,
        int slotIndex, PollEvents[] eventMasks,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq,
        RouterRouterMetrics metrics, bool allowSend)
    {
        RouterRouterClientSlot slot = slots[slotIndex];

        if (IsSocketWriteReady(pollManager, slotIndex)
            && slot.WaitingForWritable
            && !slot.WaitingForReply)
        {
            if (TrySend(slot))
            {
                slot.WaitingForReply = true;
                slot.WaitingForWritable = false;
                UpdatePollMask(slot, eventMasks, slotIndex);
            }
        }

        bool readReady = IsSocketReadReady(pollManager, slotIndex);
        if (!readReady && !slot.WaitingForReply)
            return;

        while (true)
        {
            using Received? receivedMessage = TryRecvNoWait((RouterSocket)slot.Socket);
            if (receivedMessage == null || receivedMessage.Count == 0)
                break;

            if (!slot.WaitingForReply)
                continue;

            slot.WaitingForReply = false;
            if (phase == PerfPhase.Active)
            {
                ReadOnlySpan<byte> body = receivedMessage.SinglePartOrThrow()
                    .AsReadOnlySpan();
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
                            ReservoirSample(metrics.LatencySamples,
                                sampleLatencyNs, ref sampleSeen,
                                metrics.LatencySampleCap, ref rng);
                            metrics.SampleSeen = sampleSeen;
                            metrics.Rng = rng;
                        }
                    }
                }
            }

            if (!allowSend)
                continue;

            slot.WaitingForWritable = false;
            UpdatePollMask(slot, eventMasks, slotIndex);
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
        if (slot.WaitingForWritable && !slot.WaitingForReply)
            events |= SocketPollOut;
        eventMasks[index] = events;
    }

    private static void PreparePayload(RouterRouterClientSlot slot, int msgSize,
        uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq++, EpochNs());
        slot.WaitingForWritable = false;
    }

    private static bool TrySend(RouterRouterClientSlot slot)
    {
        using Message message = Message.FromBytes(slot.Payload);
        return ((RouterSocket)slot.Socket).TrySend(slot.ServerRoutingId, message);
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
                using Message stop = Message.FromBytes(MultiStopToken);
                ((RouterSocket)activeClients[i]).Send(
                    RoutingId.FromBytes(serverRoutingId), stop,
                    SendFlags.DontWait);
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.InternalErrno)
                                            || IsInterrupted(ex.InternalErrno)
                                            || IsTransientNetworkError(ex.InternalErrno))
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }

    private sealed class RouterRouterClientSlot
    {
        internal RouterRouterClientSlot(SocketBase socket, RoutingId serverRoutingId,
            byte[] payload)
        {
            Socket = socket;
            ServerRoutingId = serverRoutingId;
            Payload = payload;
        }

        internal SocketBase Socket { get; }
        internal RoutingId ServerRoutingId { get; }
        internal byte[] Payload { get; }
        internal bool WaitingForWritable { get; set; }
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

    private static Received? TryRecvNoWait(RouterSocket socket)
    {
        return socket.TryRecv(out Received? received) ? received : null;
    }
}
