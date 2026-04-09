using System;
using System.Collections.Generic;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfStreamServer
{
    private const string Pattern = "STREAM";
    private const int StreamRoutingIdBytes = 255;

    private enum SendStatus
    {
        Done = 0,
        Blocked = 1,
        Fatal = 2,
    }

    private enum RelayStatus
    {
        Idle = 0,
        Progress = 1,
        Error = 2,
    }

    internal static int Run(PerfOptions options)
    {
        int size = Math.Max(1, options.Size);
        if (!IsCoreStreamServerTransport(options.Transport))
        {
            Console.WriteLine($"UNSUPPORTED,current,{Pattern},{options.Transport}");
            return 0;
        }

        int ioTimeoutMs = options.StreamTimeoutMs;
        int pendingCapacity = ResolvePendingCapacity(options);
        string endpoint = MultiEndpointFor(options.Transport, "multi-stream",
            options);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var server = new StreamSocket(ctx);
        ApplyMultiSocketOptions(server, options);
        ConfigureTlsServerIfNeeded(server, options.Transport);
        server.SetOption(SocketOptions.SndTimeo, ioTimeoutMs);
        server.SetOption(SocketOptions.RcvTimeo, ioTimeoutMs);
        server.SetOption(SocketOptions.TcpNoDelay, 1);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        var pending = CreatePendingMessages(pendingCapacity);
        int pendingCount = 0;
        var control = new ControlState();
        StartControlWatcher(control);

        using var poller = new Poller();
        poller.Add(server, PollEvents.PollIn);
        var events = new List<PollEvent>(1);

        int rc = 0;
        while (Volatile.Read(ref control.StopRequested) == 0)
        {
            poller.Modify(server, pendingCount > 0
                ? PollEvents.PollIn | PollEvents.PollOut
                : PollEvents.PollIn);
            if (!WaitForEvents(poller, events, 50))
                continue;

            for (int i = 0; i < events.Count; i++)
            {
                PollEvent pollEvent = events[i];
                if ((pollEvent.Revents & PollEvents.PollOut) != 0
                    && pendingCount > 0
                    && !FlushPendingMessages(server, pending, ref pendingCount))
                {
                    rc = 1;
                    break;
                }

                if ((pollEvent.Revents & PollEvents.PollIn) == 0)
                    continue;

                while (Volatile.Read(ref control.StopRequested) == 0)
                {
                    RelayStatus status = RelayStreamMessageNonBlocking(server, pending,
                        ref pendingCount);
                    if (status == RelayStatus.Error)
                    {
                        rc = 1;
                        break;
                    }
                    if (status == RelayStatus.Idle)
                        break;
                }
                if (rc != 0)
                    break;
            }
        }

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

    private static int ResolvePendingCapacity(PerfOptions options)
    {
        int clients = options.Clients;
        int hwm = options.MultiHwm;
        long capacity = Math.Max(64L, Math.Max(clients, Math.Max(1, hwm)) * 2L);
        return capacity > int.MaxValue ? int.MaxValue : (int)capacity;
    }

    private static PendingStreamMessage[] CreatePendingMessages(int capacity)
    {
        var pending = new PendingStreamMessage[capacity];
        for (int i = 0; i < pending.Length; i++)
            pending[i] = new PendingStreamMessage();
        return pending;
    }

    private static RelayStatus RelayStreamMessageNonBlocking(SocketBase server,
        PendingStreamMessage[] pending, ref int pendingCount)
    {
        Message? idFrame = TryReceiveStreamFrame(server);
        if (idFrame == null)
            return RelayStatus.Idle;

        try
        {
            if (server.GetOption(SocketOptions.RcvMore) == 0)
                return RelayStatus.Error;

            ReadOnlySpan<byte> routingId = idFrame.AsReadOnlySpan();

            while (true)
            {
                Message? payloadFrame = TryReceiveStreamFrame(server);
                if (payloadFrame == null)
                    return RelayStatus.Idle;

                bool more = server.GetOption(SocketOptions.RcvMore) != 0;
                try
                {
                    ReadOnlySpan<byte> payload = payloadFrame.AsReadOnlySpan();
                    if (!IsStreamEventPayload(payload))
                    {
                        var request = new PendingStreamMessage();
                        request.Assign(routingId, payloadFrame);
                        payloadFrame = null;
                        SendStatus sendStatus = TrySendPendingMessage(server, request);
                        if (sendStatus == SendStatus.Blocked)
                        {
                            if (!EnqueuePendingMessage(pending, ref pendingCount,
                                    request))
                                return RelayStatus.Error;
                        }
                        else if (sendStatus == SendStatus.Fatal)
                        {
                            request.Clear();
                            return RelayStatus.Error;
                        }
                    }
                }
                finally
                {
                    payloadFrame?.Dispose();
                }

                if (!more)
                    break;
            }

            return RelayStatus.Progress;
        }
        finally
        {
            idFrame.Dispose();
        }
    }

    private static Message? TryReceiveStreamFrame(SocketBase socket)
    {
        try
        {
            return socket.ReceiveMessage(ReceiveFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            return null;
        }
    }

    private static bool FlushPendingMessages(SocketBase server,
        PendingStreamMessage[] pending, ref int pendingCount)
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

    private static bool EnqueuePendingMessage(PendingStreamMessage[] pending,
        ref int pendingCount, PendingStreamMessage message)
    {
        if (pendingCount >= pending.Length)
            return false;
        pending[pendingCount].MoveFrom(message);
        pendingCount++;
        return true;
    }

    private static void ErasePendingMessage(PendingStreamMessage[] pending,
        ref int pendingCount, int index)
    {
        int last = pendingCount - 1;
        pending[index].Clear();
        if (index != last)
            pending[index].MoveFrom(pending[last]);
        pending[last].Clear();
        pendingCount--;
    }

    private static SendStatus TrySendPendingMessage(SocketBase server,
        PendingStreamMessage message)
    {
        while (message.Stage != StreamSendStage.None)
        {
            try
            {
                if (message.Stage == StreamSendStage.RoutingId)
                {
                    int rc = server.Send(message.RoutingId.AsSpan(0,
                            message.RoutingIdLength),
                        SendFlags.SendMore | SendFlags.DontWait);
                    if (rc <= 0)
                        return SendStatus.Fatal;
                    message.Stage = StreamSendStage.Payload;
                    continue;
                }

                if (message.Payload == null)
                    return SendStatus.Fatal;

                int sent = server.Send(message.Payload.AsReadOnlySpan(),
                    SendFlags.DontWait);
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

        return SendStatus.Done;
    }

    private static bool IsStreamEventPayload(ReadOnlySpan<byte> payload)
    {
        return payload.Length == 0
            || (payload.Length == 1 && (payload[0] == 0x00 || payload[0] == 0x01));
    }

    private enum StreamSendStage
    {
        None = 0,
        RoutingId = 1,
        Payload = 2,
    }

    private sealed class PendingStreamMessage
    {
        internal PendingStreamMessage()
        {
            RoutingId = new byte[StreamRoutingIdBytes];
        }

        internal byte[] RoutingId { get; }
        internal int RoutingIdLength { get; private set; }
        internal Message? Payload { get; private set; }
        internal StreamSendStage Stage { get; set; }

        internal void Assign(ReadOnlySpan<byte> routingId, Message payload)
        {
            int length = Math.Min(routingId.Length, RoutingId.Length);
            routingId.Slice(0, length).CopyTo(RoutingId);
            RoutingIdLength = length;
            Payload = payload;
            Stage = StreamSendStage.RoutingId;
        }

        internal void MoveFrom(PendingStreamMessage other)
        {
            Clear();
            if (other.RoutingIdLength > 0)
            {
                other.RoutingId.AsSpan(0, other.RoutingIdLength)
                    .CopyTo(RoutingId);
            }
            RoutingIdLength = other.RoutingIdLength;
            Payload = other.Payload;
            Stage = other.Stage;
            other.Payload = null;
            other.RoutingIdLength = 0;
            other.Stage = StreamSendStage.None;
        }

        internal void Clear()
        {
            Payload?.Dispose();
            Payload = null;
            RoutingIdLength = 0;
            Stage = StreamSendStage.None;
        }
    }

    private sealed class ControlState
    {
        internal int StopRequested;
    }
}
