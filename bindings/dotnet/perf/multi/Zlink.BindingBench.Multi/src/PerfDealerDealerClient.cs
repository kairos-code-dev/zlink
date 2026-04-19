using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerDealerClient
{
    private const int SendBackoffPollTimeoutMs = 50;

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        int warmupSeconds = ResolveMultiWarmupSeconds(options);
        int durationSeconds = ResolveMultiDurationSeconds(options);
        int sndTimeoutMs = ResolveMultiSndTimeoutMs(options);
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs(options);
        int readyTimeoutMs = ResolveMultiConnectReadyTimeoutMs(options);
        int clientCount = ResolveMultiClients(options);
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
            if (!RunMultiDealerDealerClientLoop(pollManager, slots, size,
                    warmupSeconds, durationSeconds))
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
        PollManager pollManager, DealerDealerClientSlot[] slots, int msgSize,
        int warmupSeconds, int durationSeconds)
    {
        const uint runId = 1;
        ulong seq = 1;
        var sockets = CollectSockets(slots);
        var eventMasks = new PollEvents[slots.Length];

        RunSendPhase(pollManager, slots, sockets, eventMasks, msgSize,
            warmupSeconds, PerfPhase.Warmup, runId, ref seq, sendActive: true);

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
                    && !IsSocketWriteReady(pollManager, index))
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

            if (PollSocketEvents(pollManager, sockets, eventMasks,
                    ResolveSendPollTimeoutMs(false, benchDeadlineTicks)) <= 0)
                continue;
        }

        return anySent;
    }

    private static void RunSendPhase(PollManager pollManager,
        DealerDealerClientSlot[] slots,
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
                    if (!slot.WaitingForWritable
                        || IsSocketWriteReady(pollManager, index))
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

            if (PollSocketEvents(pollManager, sockets, eventMasks,
                    ResolveSendPollTimeoutMs(progressed, deadlineTicks)) <= 0)
                continue;
        }
    }

    private static bool TrySendDealerDealer(ref DealerDealerClientSlot slot,
        int msgSize, uint runId, PerfPhase phase, ref ulong seq)
    {
        StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
            seq, EpochNs());

        try
        {
            bool sent = slot.Socket.SendNoWaitResult(slot.Payload.AsSpan(),
                PerfSendFlags.DontWait, out int written) && written > 0;
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
