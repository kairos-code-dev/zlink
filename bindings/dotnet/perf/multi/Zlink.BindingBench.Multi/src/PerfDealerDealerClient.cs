using System;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfDealerDealerClient
{
    internal static int Run(string transport, int size, string endpoint)
    {
        const string pattern = "DEALER_DEALER";
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
            var result = RunMultiDealerDealerClientLoop(slots, size,
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

    private static DealerDealerClientSlot[] CreateSlots(
        List<Zlink.Socket> activeClients, int msgSize)
    {
        var slots = new DealerDealerClientSlot[activeClients.Count];
        for (int i = 0; i < activeClients.Count; i++)
        {
            var payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
            Array.Fill(payload, (byte)'a');
            slots[i] = new DealerDealerClientSlot(activeClients[i], payload);
        }

        return slots;
    }

    private static (double throughput, double latencyUs, double latencyP95Us,
        double latencyP99Us)
        RunMultiDealerDealerClientLoop(DealerDealerClientSlot[] slots,
            int msgSize, int latencySampleCap, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs)
    {
        const uint runId = 1;
        var latSamples = new List<double>(latencySampleCap);
        long sampleSeen = 0;
        uint rng = 0xA341316Cu;
        ulong seq = 1;

        using var poller = new Poller();
        var pollEvents = new List<PollEvent>(slots.Length);

        RunWarmupPhase(slots, poller, pollEvents, msgSize, warmupSeconds,
            activeWarmup, warmupDrainMs, runId, ref seq);

        if (settleMs > 0)
            Thread.Sleep(settleMs);

        long measureCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        int index = 0;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Length; i++)
            {
                ref DealerDealerClientSlot slot = ref slots[index];
                long beginTicks = Stopwatch.GetTimestamp();
                if (TrySendDealerDealer(slot.Socket, slot.Payload, msgSize, runId,
                        PerfPhase.Active, ref seq))
                {
                    long endTicks = Stopwatch.GetTimestamp();
                    measureCount++;
                    progressed = true;
                    double sampleLatencyUs = (endTicks - beginTicks) * 1_000_000.0
                        / Stopwatch.Frequency;
                    ReservoirSample(latSamples, sampleLatencyUs, ref sampleSeen,
                        latencySampleCap, ref rng);
                    ClearPollOut(slot, poller);
                }
                else if (!slot.WaitingForWritable)
                {
                    slot.WaitingForWritable = true;
                    poller.Add(slot.Socket, PollEvents.PollOut, slot.Socket);
                }

                index++;
                if (index == slots.Length)
                    index = 0;
            }

            if (progressed || poller.Count == 0)
                continue;

            if (!WaitForEvents(poller, pollEvents,
                    RemainingMilliseconds(benchDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < pollEvents.Count; i++)
            {
                Zlink.Socket? socket = pollEvents[i].Socket;
                if (socket == null || (pollEvents[i].Revents & PollEvents.PollOut) == 0)
                    continue;

                int slotIndex = FindSlot(slots, socket);
                if (slotIndex < 0)
                    continue;

                ref DealerDealerClientSlot slot = ref slots[slotIndex];
                if (TrySendDealerDealer(slot.Socket, slot.Payload, msgSize, runId,
                        PerfPhase.Active, ref seq))
                {
                    measureCount++;
                    ClearPollOut(slot, poller);
                }
            }
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
            / Math.Max(1.0, measureCount);
        var latency = ComputeLatencyStats(latSamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;

        return (throughput, latencyUs, latencyP95Us, latencyP99Us);
    }

    private static void RunWarmupPhase(DealerDealerClientSlot[] slots,
        Poller poller, List<PollEvent> pollEvents, int msgSize, int warmupSeconds,
        bool activeWarmup, int warmupDrainMs, uint runId, ref ulong seq)
    {
        if (!activeWarmup || warmupSeconds <= 0)
            return;

        long warmupDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, warmupSeconds) * Stopwatch.Frequency;
        int index = 0;
        while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Length; i++)
            {
                ref DealerDealerClientSlot slot = ref slots[index];
                if (TrySendDealerDealer(slot.Socket, slot.Payload, msgSize, runId,
                        PerfPhase.Warmup, ref seq))
                {
                    progressed = true;
                    ClearPollOut(slot, poller);
                }
                else if (!slot.WaitingForWritable)
                {
                    slot.WaitingForWritable = true;
                    poller.Add(slot.Socket, PollEvents.PollOut, slot.Socket);
                }

                index++;
                if (index == slots.Length)
                    index = 0;
            }

            if (progressed || poller.Count == 0)
                continue;

            if (!WaitForEvents(poller, pollEvents,
                    RemainingMilliseconds(warmupDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < pollEvents.Count; i++)
            {
                Zlink.Socket? socket = pollEvents[i].Socket;
                if (socket == null || (pollEvents[i].Revents & PollEvents.PollOut) == 0)
                    continue;

                int slotIndex = FindSlot(slots, socket);
                if (slotIndex < 0)
                    continue;

                ref DealerDealerClientSlot slot = ref slots[slotIndex];
                if (TrySendDealerDealer(slot.Socket, slot.Payload, msgSize, runId,
                        PerfPhase.Warmup, ref seq))
                {
                    ClearPollOut(slot, poller);
                }
            }
        }

        if (warmupDrainMs > 0)
            Thread.Sleep(warmupDrainMs);
    }

    private static bool TrySendDealerDealer(Zlink.Socket socket, byte[] payload,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(payload.AsSpan(), runId, phase, msgSize, seq++, EpochUs());
        try
        {
            return socket.Send(payload.AsSpan(), SendFlags.DontWait) > 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            return false;
        }
    }

    private static void ClearPollOut(DealerDealerClientSlot slot, Poller poller)
    {
        if (!slot.WaitingForWritable)
            return;

        slot.WaitingForWritable = false;
        _ = poller.Remove(slot.Socket);
    }

    private static int FindSlot(DealerDealerClientSlot[] slots, Zlink.Socket socket)
    {
        for (int i = 0; i < slots.Length; i++)
        {
            if (ReferenceEquals(slots[i].Socket, socket))
                return i;
        }

        return -1;
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

    private sealed class DealerDealerClientSlot
    {
        internal DealerDealerClientSlot(Zlink.Socket socket, byte[] payload)
        {
            Socket = socket;
            Payload = payload;
        }

        internal Zlink.Socket Socket { get; }
        internal byte[] Payload { get; }
        internal bool WaitingForWritable { get; set; }
    }
}
