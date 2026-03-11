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
        int sndTimeoutMs = ResolveMultiSndTimeoutMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs();
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
            if (!RunMultiDealerDealerClientLoop(slots, size, warmupSeconds,
                    durationSeconds, settleMs, drainMs,
                    sizeTransitionDrainMs))
            {
                return 2;
            }

            TrySendStopToken(activeClients);
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

    private static bool RunMultiDealerDealerClientLoop(
        DealerDealerClientSlot[] slots, int msgSize, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs)
    {
        const uint runId = 1;
        ulong seq = 1;

        using var poller = new Poller();
        var pollEvents = new List<PollEvent>(slots.Length);

        RunSendPhase(slots, poller, pollEvents, msgSize, warmupSeconds,
            PerfPhase.Warmup, runId, ref seq, sendActive: true);
        RunSendPhase(slots, poller, pollEvents, msgSize, settleMs / 1000,
            PerfPhase.Drain, runId, ref seq, sendActive: false);

        long benchDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(1, durationSeconds) * Stopwatch.Frequency;
        int index = 0;
        bool anySent = false;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Length; i++)
            {
                ref DealerDealerClientSlot slot = ref slots[index];
                if (TrySendDealerDealer(ref slot, poller, msgSize, runId,
                        PerfPhase.Active, ref seq))
                {
                    progressed = true;
                    anySent = true;
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
                if (TrySendDealerDealer(ref slot, poller, msgSize, runId,
                        PerfPhase.Active, ref seq))
                {
                    anySent = true;
                }
            }
        }

        if (drainMs > 0)
            Thread.Sleep(drainMs);
        if (sizeTransitionDrainMs > 0)
            Thread.Sleep(sizeTransitionDrainMs);

        return anySent;
    }

    private static void RunSendPhase(DealerDealerClientSlot[] slots,
        Poller poller, List<PollEvent> pollEvents, int msgSize, int durationSeconds,
        PerfPhase phase, uint runId, ref ulong seq, bool sendActive)
    {
        if (durationSeconds <= 0)
            return;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, durationSeconds) * Stopwatch.Frequency;
        int index = 0;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Length; i++)
            {
                ref DealerDealerClientSlot slot = ref slots[index];
                if (sendActive
                    && TrySendDealerDealer(ref slot, poller, msgSize, runId,
                        phase, ref seq))
                {
                    progressed = true;
                }

                index++;
                if (index == slots.Length)
                    index = 0;
            }

            if (progressed || poller.Count == 0)
                continue;

            if (!WaitForEvents(poller, pollEvents,
                    RemainingMilliseconds(deadlineTicks)))
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
                if (TrySendDealerDealer(ref slot, poller, msgSize, runId,
                        phase, ref seq))
                {
                    progressed = true;
                }
            }
        }
    }

    private static bool TrySendDealerDealer(ref DealerDealerClientSlot slot,
        Poller poller, int msgSize, uint runId, PerfPhase phase, ref ulong seq)
    {
        if (!slot.WaitingForWritable)
            StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
                seq++, EpochUs());

        try
        {
            bool sent = slot.Socket.TrySend(slot.Payload.AsSpan(),
                SendFlags.DontWait, out int written) && written > 0;
            if (sent)
                ClearPollOut(ref slot, poller);
            else if (!slot.WaitingForWritable)
                MarkPollOut(ref slot, poller);
            return sent;
        }
        catch (ZlinkException)
        {
            throw;
        }
    }

    private static void MarkPollOut(ref DealerDealerClientSlot slot, Poller poller)
    {
        slot.WaitingForWritable = true;
        poller.Add(slot.Socket, PollEvents.PollOut, slot.Socket);
    }

    private static void ClearPollOut(ref DealerDealerClientSlot slot, Poller poller)
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
