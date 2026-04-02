using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerDealerClient
{
    private const int SendBackoffPollTimeoutMs = 50;

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
        var clients = new List<SocketBase>(clientCount);
        var monitors = new List<MonitorSocket>(clientCount);
        try
        {
            for (int i = 0; i < clientCount; i++)
            {
                var client = new DealerSocket(ctx);
                ApplyMultiSocketOptions(client, pattern);
                ConfigureTlsClientIfNeeded(client, transport);
                client.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
                client.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
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
        List<SocketBase> activeClients, int msgSize)
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
        var sockets = CollectSockets(slots);
        var eventMasks = new PollEvents[slots.Length];

        RunSendPhase(slots, sockets, eventMasks, msgSize, warmupSeconds,
            PerfPhase.Warmup, runId, ref seq, sendActive: true);
        RunSendPhase(slots, sockets, eventMasks, msgSize, settleMs / 1000.0,
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
                if (slot.WaitingForWritable
                    && !IsSocketWriteReady(index))
                {
                    index++;
                    if (index == slots.Length)
                        index = 0;
                    continue;
                }

                if (TrySendDealerDealer(ref slot, msgSize, runId,
                        PerfPhase.Active, ref seq))
                {
                    progressed = true;
                    anySent = true;
                }

                index++;
                if (index == slots.Length)
                    index = 0;
            }

            ResetPollMasks(slots, eventMasks);
            if (!HasPendingSend(slots))
            {
                if (progressed)
                    continue;

                int idleTimeoutMs = ResolveSendPollTimeoutMs(false,
                    benchDeadlineTicks);
                if (idleTimeoutMs > 0)
                    Thread.Sleep(idleTimeoutMs);
                continue;
            }

            if (PollSocketEvents(sockets, eventMasks,
                    ResolveSendPollTimeoutMs(false, benchDeadlineTicks)) <= 0)
                continue;
        }

        if (drainMs > 0)
            Thread.Sleep(drainMs);
        if (sizeTransitionDrainMs > 0)
            Thread.Sleep(sizeTransitionDrainMs);

        return anySent;
    }

    private static void RunSendPhase(DealerDealerClientSlot[] slots,
        IReadOnlyList<SocketBase> sockets, PollEvents[] eventMasks, int msgSize,
        double durationSeconds,
        PerfPhase phase, uint runId, ref ulong seq, bool sendActive)
    {
        if (durationSeconds <= 0.0)
            return;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)(Math.Max(0.0, durationSeconds) * Stopwatch.Frequency);
        int index = 0;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Length; i++)
            {
                ref DealerDealerClientSlot slot = ref slots[index];
                if (sendActive)
                {
                    if (!slot.WaitingForWritable || IsSocketWriteReady(index))
                    {
                        if (TrySendDealerDealer(ref slot, msgSize, runId,
                                phase, ref seq))
                            progressed = true;
                    }
                }
                else
                {
                    slot.WaitingForWritable = false;
                }

                index++;
                if (index == slots.Length)
                    index = 0;
            }

            ResetPollMasks(slots, eventMasks);
            if (!HasPendingSend(slots))
            {
                if (progressed || sendActive)
                    continue;

                int idleTimeoutMs = ResolveSendPollTimeoutMs(false,
                    deadlineTicks);
                if (idleTimeoutMs > 0)
                    Thread.Sleep(idleTimeoutMs);
                continue;
            }

            if (PollSocketEvents(sockets, eventMasks,
                    ResolveSendPollTimeoutMs(progressed, deadlineTicks)) <= 0)
                continue;
        }
    }

    private static bool TrySendDealerDealer(ref DealerDealerClientSlot slot,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq, EpochUs());

        try
        {
            bool sent = slot.Socket.TrySend(slot.Payload.AsSpan(),
                SendFlags.DontWait, out int written) && written > 0;
            if (sent)
            {
                slot.WaitingForWritable = false;
                seq++;
            }
            else
            {
                slot.WaitingForWritable = true;
            }
            return sent;
        }
        catch (ZlinkException)
        {
            throw;
        }
    }

    private static List<SocketBase> CollectSockets(
        DealerDealerClientSlot[] slots)
    {
        var sockets = new List<SocketBase>(slots.Length);
        for (int i = 0; i < slots.Length; i++)
            sockets.Add(slots[i].Socket);
        return sockets;
    }

    private static void ResetPollMasks(DealerDealerClientSlot[] slots,
        PollEvents[] eventMasks)
    {
        for (int i = 0; i < slots.Length; i++)
            eventMasks[i] = slots[i].WaitingForWritable
                ? SocketPollOut
                : PollEvents.None;
    }

    private static bool HasPendingSend(DealerDealerClientSlot[] slots)
    {
        for (int i = 0; i < slots.Length; i++)
        {
            if (slots[i].WaitingForWritable)
                return true;
        }

        return false;
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

    private static int ResolveSendPollTimeoutMs(bool progressed,
        long deadlineTicks)
    {
        if (progressed)
            return 0;

        return Math.Min(SendBackoffPollTimeoutMs,
            RemainingMilliseconds(deadlineTicks));
    }

    private sealed class DealerDealerClientSlot
    {
        internal DealerDealerClientSlot(SocketBase socket, byte[] payload)
        {
            Socket = socket;
            Payload = payload;
        }

        internal SocketBase Socket { get; }
        internal byte[] Payload { get; }
        internal bool WaitingForWritable { get; set; }
    }
}
