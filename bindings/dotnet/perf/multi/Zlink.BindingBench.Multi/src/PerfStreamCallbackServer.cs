using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfStreamCallbackServer
{
    private const string Pattern = "STREAM_CALLBACK";
    private const int StreamBacklogFloor = 4096;
    private const int ReadyDelayMs = 200;

    private enum SendStatus
    {
        Done = 0,
        Blocked = 1,
        Fatal = 2,
    }

    internal static int Run(string transport, int size)
    {
        size = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        int durationSeconds = ResolveMultiDurationSeconds();
        int settleMs = ResolveMultiSettleMs();
        int rcvTimeoutMs = ResolveMultiRcvTimeoutMs();
        int settleSeconds = (settleMs + 999) / 1000;
        int backlog = Math.Max(StreamBacklogFloor, ResolveClients(Pattern));
        int pendingCapacity = ResolvePendingCapacity();
        string endpoint = MultiEndpointFor(transport, "multi-stream-callback");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplyMultiSocketOptions(server, Pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.SetOption(SocketOptions.Backlog, backlog);

        int stopRequested = 0;
        int callbackFailed = 0;
        long payloadSeen = 0;
        long lastActivityTicks = Stopwatch.GetTimestamp();
        object pendingLock = new object();
        var stopParsers = new Dictionary<uint, Len32StopTokenParser>();
        PendingStreamPacket[] pending = CreatePendingMessages(pendingCapacity);
        int pendingCount = 0;
        using var activitySignal = new AutoResetEvent(false);
        using var poller = new Poller();
        poller.Add(server, PollEvents.None);
        var pollEvents = new List<PollEvent>(1);

        StreamPacketHandler rawHandler = (rid, payload) =>
        {
            ReadOnlySpan<byte> payloadBytes = payload.AsReadOnlySpan();
            if (IsStreamEventPayload(payloadBytes))
            {
                payload.Dispose();
                return 0;
            }

            if (payloadBytes.SequenceEqual(MultiStopToken)
                || ConsumeLen32StopToken(stopParsers, pendingLock, rid,
                    payloadBytes))
            {
                Interlocked.Exchange(ref stopRequested, 1);
                payload.Dispose();
                activitySignal.Set();
                return 0;
            }

            Interlocked.Increment(ref payloadSeen);
            Interlocked.Exchange(ref lastActivityTicks, Stopwatch.GetTimestamp());

            lock (pendingLock)
            {
                var request = new PendingStreamPacket();
                request.Assign(rid, payload);
                payload = null!;
                SendStatus sendStatus = TrySendPendingMessage(server, request);
                if (sendStatus == SendStatus.Blocked)
                {
                    if (!EnqueuePendingMessage(pending, ref pendingCount, request))
                    {
                        request.Clear();
                        Interlocked.Exchange(ref callbackFailed, 1);
                        Interlocked.Exchange(ref stopRequested, 1);
                    }
                }
                else if (sendStatus == SendStatus.Fatal)
                {
                    request.Clear();
                    Interlocked.Exchange(ref callbackFailed, 1);
                    Interlocked.Exchange(ref stopRequested, 1);
                }
            }

            activitySignal.Set();
            return 0;
        };

        server.AttachStreamRaw(rawHandler);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");
        Thread.Sleep(ReadyDelayMs);

        long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(8, warmupSeconds + durationSeconds + settleSeconds + 5)
            * Stopwatch.Frequency;
        long idleBreakTicks = (long)(Stopwatch.Frequency
            * (Math.Max(rcvTimeoutMs * 2, 1000) / 1000.0));
        while (Volatile.Read(ref stopRequested) == 0)
        {
            if (Volatile.Read(ref callbackFailed) != 0)
                return 2;

            int waitMs = ComputeWaitTimeout(Volatile.Read(ref payloadSeen),
                Volatile.Read(ref lastActivityTicks), firstPacketDeadlineTicks,
                idleBreakTicks);
            if (waitMs <= 0)
                break;

            bool havePending;
            lock (pendingLock)
            {
                havePending = pendingCount > 0;
                poller.Modify(server,
                    havePending ? PollEvents.PollOut : PollEvents.None);
            }

            if (!havePending)
            {
                activitySignal.WaitOne(Math.Min(waitMs, 50));
                continue;
            }

            if (!WaitForEvents(poller, pollEvents, waitMs))
                continue;

            bool writable = false;
            for (int i = 0; i < pollEvents.Count; i++)
            {
                if ((pollEvents[i].Revents & PollEvents.PollOut) != 0)
                {
                    writable = true;
                    break;
                }
            }
            if (!writable)
                continue;

            lock (pendingLock)
            {
                if (!FlushPendingMessages(server, pending, ref pendingCount))
                {
                    Interlocked.Exchange(ref callbackFailed, 1);
                    Interlocked.Exchange(ref stopRequested, 1);
                    return 2;
                }
            }
        }

        try
        {
            server.DetachStream();
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
        }

        return Volatile.Read(ref callbackFailed) == 0 ? 0 : 2;
    }

    private static int ResolvePendingCapacity()
    {
        int clients = ResolveClients(Pattern);
        int hwm = ResolveHwm(Pattern);
        long capacity = Math.Max(64L, Math.Max(clients, Math.Max(1, hwm)) * 2L);
        return capacity > int.MaxValue ? int.MaxValue : (int)capacity;
    }

    private static int ComputeWaitTimeout(long payloadSeen,
        long lastActivityTicks, long firstPacketDeadlineTicks,
        long idleBreakTicks)
    {
        long nowTicks = Stopwatch.GetTimestamp();
        long remainingTicks = payloadSeen > 0
            ? idleBreakTicks - (nowTicks - lastActivityTicks)
            : firstPacketDeadlineTicks - nowTicks;
        if (remainingTicks <= 0)
            return 0;
        long timeoutMs = (remainingTicks * 1000 + Stopwatch.Frequency - 1)
            / Stopwatch.Frequency;
        return timeoutMs <= 0 ? 1 : timeoutMs > int.MaxValue
            ? int.MaxValue
            : (int)timeoutMs;
    }

    private static PendingStreamPacket[] CreatePendingMessages(int capacity)
    {
        var pending = new PendingStreamPacket[capacity];
        for (int i = 0; i < pending.Length; i++)
            pending[i] = new PendingStreamPacket();
        return pending;
    }

    private static bool EnqueuePendingMessage(PendingStreamPacket[] pending,
        ref int pendingCount, PendingStreamPacket message)
    {
        if (pendingCount >= pending.Length)
            return false;
        pending[pendingCount].MoveFrom(message);
        pendingCount++;
        return true;
    }

    private static bool FlushPendingMessages(Zlink.Socket server,
        PendingStreamPacket[] pending, ref int pendingCount)
    {
        int index = 0;
        while (index < pendingCount)
        {
            SendStatus sendStatus = TrySendPendingMessage(server, pending[index]);
            if (sendStatus == SendStatus.Done)
            {
                ErasePendingMessage(pending, ref pendingCount, index);
                continue;
            }
            if (sendStatus == SendStatus.Fatal)
                return false;
            index++;
        }

        return true;
    }

    private static void ErasePendingMessage(PendingStreamPacket[] pending,
        ref int pendingCount, int index)
    {
        int last = pendingCount - 1;
        pending[index].Clear();
        if (index != last)
            pending[index].MoveFrom(pending[last]);
        pending[last].Clear();
        pendingCount--;
    }

    private static SendStatus TrySendPendingMessage(Zlink.Socket server,
        PendingStreamPacket message)
    {
        if (!message.HasPayload || message.Payload == null)
            return SendStatus.Fatal;

        try
        {
            int sent = server.StreamSend(message.RoutingId,
                message.Payload.AsReadOnlySpan(), SendFlags.DontWait);
            if (sent < 0)
                return SendStatus.Fatal;
            message.Clear();
            return SendStatus.Done;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            return SendStatus.Blocked;
        }
    }

    private static bool ConsumeLen32StopToken(
        Dictionary<uint, Len32StopTokenParser> stopParsers, object sync,
        uint routingId, ReadOnlySpan<byte> payload)
    {
        lock (sync)
        {
            if (!stopParsers.TryGetValue(routingId, out Len32StopTokenParser? parser))
            {
                parser = new Len32StopTokenParser();
                stopParsers[routingId] = parser;
            }
            return parser.Consume(payload);
        }
    }

    private static bool IsStreamEventPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == 1 && (payload[0] == 0x00 || payload[0] == 0x01);
    }

    private sealed class PendingStreamPacket
    {
        internal uint RoutingId { get; private set; }
        internal Message? Payload { get; private set; }
        internal bool HasPayload => Payload != null;

        internal void Assign(uint routingId, Message payload)
        {
            RoutingId = routingId;
            Payload = payload;
        }

        internal void MoveFrom(PendingStreamPacket other)
        {
            Clear();
            RoutingId = other.RoutingId;
            Payload = other.Payload;
            other.RoutingId = 0;
            other.Payload = null;
        }

        internal void Clear()
        {
            Payload?.Dispose();
            Payload = null;
            RoutingId = 0;
        }
    }
}
