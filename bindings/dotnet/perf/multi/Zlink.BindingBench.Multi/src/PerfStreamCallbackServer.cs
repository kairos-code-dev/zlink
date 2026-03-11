using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfStreamCallbackServer
{
    private const string Pattern = "STREAM_CALLBACK";

    private enum SendStatus
    {
        Done = 0,
        Blocked = 1,
        Fatal = 2,
    }

    internal static int Run(string transport, int size)
    {
        size = Math.Max(1, size);
        if (!IsCoreStreamServerTransport(transport))
        {
            Console.WriteLine($"UNSUPPORTED,current,{Pattern},{transport}");
            return 0;
        }

        int ioTimeoutMs = ResolveStreamIoTimeoutMs();
        int pendingCapacity = ResolvePendingCapacity();
        string endpoint = MultiEndpointFor(transport, "multi-stream-callback");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplyMultiSocketOptions(server, Pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.SndTimeo, ioTimeoutMs);
        server.SetOption(SocketOptions.RcvTimeo, ioTimeoutMs);
        server.SetOption(SocketOptions.TcpNoDelay, 1);

        var control = new ControlState();
        int callbackFailed = 0;
        object pendingLock = new();
        PendingStreamPacket[] pending = CreatePendingMessages(pendingCapacity);
        int pendingCount = 0;

        StreamPacketHandler rawHandler = (rid, payload) =>
        {
            ReadOnlySpan<byte> payloadBytes = payload.AsReadOnlySpan();
            if (IsStreamEventPayload(payloadBytes))
            {
                payload.Dispose();
                return 0;
            }

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
                        return 1;
                    }
                }
                else if (sendStatus == SendStatus.Fatal)
                {
                    request.Clear();
                    Interlocked.Exchange(ref callbackFailed, 1);
                    return 1;
                }
            }

            return 0;
        };

        server.AttachStreamRaw(rawHandler);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");
        StartControlWatcher(control);

        int rc = 0;
        using var poller = new Poller();
        poller.Add(server, PollEvents.None);
        var pollEvents = new List<PollEvent>(1);
        while (Volatile.Read(ref control.StopRequested) == 0)
        {
            EmitRequestedQueueProbe(server, transport, size, control);
            if (Volatile.Read(ref callbackFailed) != 0)
            {
                rc = 1;
                break;
            }

            bool havePending;
            lock (pendingLock)
            {
                havePending = pendingCount > 0;
                poller.Modify(server,
                    havePending ? PollEvents.PollOut : PollEvents.None);
            }

            if (!havePending)
            {
                Thread.Sleep(50);
                continue;
            }

            if (!WaitForEvents(poller, pollEvents, 50))
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
                    rc = 1;
                    break;
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

        PrintServerQueueMetrics(Pattern, transport, size,
            SampleServerQueueStats(server));
        return rc;
    }

    private static void StartControlWatcher(ControlState control)
    {
        Thread watcher = new(() =>
        {
            try
            {
                string? line;
                while ((line = Console.In.ReadLine()) != null)
                {
                    if (TryParseQueueProbe(line, out int requestedSize))
                    {
                        Interlocked.Exchange(ref control.QueueProbeSize, requestedSize);
                        Interlocked.Exchange(ref control.QueueProbePending, 1);
                        continue;
                    }
                    if (line == "STOP" || line == "QUIT")
                    {
                        Interlocked.Exchange(ref control.StopRequested, 1);
                        return;
                    }
                }
            }
            finally
            {
                Interlocked.Exchange(ref control.StopRequested, 1);
            }
        });
        watcher.IsBackground = true;
        watcher.Start();
    }

    private static bool TryParseQueueProbe(string line, out int size)
    {
        size = 0;
        if (string.IsNullOrWhiteSpace(line)
            || !line.StartsWith("QUEUE,", StringComparison.Ordinal))
            return false;
        return int.TryParse(line.AsSpan("QUEUE,".Length), out size) && size > 0;
    }

    private static void EmitRequestedQueueProbe(Zlink.Socket server,
        string transport, int fallbackSize, ControlState control)
    {
        if (Interlocked.Exchange(ref control.QueueProbePending, 0) == 0)
            return;
        int size = (int)Math.Max(1L, Interlocked.Read(ref control.QueueProbeSize));
        if (size <= 0)
            size = fallbackSize;
        PrintServerQueueMetrics(Pattern, transport, size,
            SampleServerQueueStats(server));
    }

    private static int ResolvePendingCapacity()
    {
        int clients = ResolveClients(Pattern);
        int hwm = ResolveHwm(Pattern);
        long capacity = Math.Max(64L, Math.Max(clients, Math.Max(1, hwm)) * 2L);
        return capacity > int.MaxValue ? int.MaxValue : (int)capacity;
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

    private static bool IsStreamEventPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == 0;
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

    private sealed class ControlState
    {
        internal int StopRequested;
        internal long QueueProbeSize;
        internal int QueueProbePending;
    }
}
