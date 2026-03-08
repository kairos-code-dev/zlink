using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using Zlink;
using static PerfRunner;

internal static class PerfStreamServer
{
    private const string Pattern = "STREAM";
    private const int StreamRoutingIdBytes = 255;
    private const int StreamBacklogFloor = 4096;
    private const int ReadyDelayMs = 200;

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
        Stop = 2,
        Error = 3,
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
        string endpoint = MultiEndpointFor(transport, "multi-stream");

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var server = new Zlink.Socket(ctx, Zlink.SocketType.Stream);
        ApplyMultiSocketOptions(server, Pattern);
        ConfigureTlsServerIfNeeded(server, transport);
        server.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
        server.SetOption(SocketOptions.Backlog, backlog);
        server.Bind(endpoint);
        Console.WriteLine($"READY,{endpoint}");

        var pending = CreatePendingMessages(pendingCapacity);
        var stopParsers = new Dictionary<RoutingKey, Len32StopTokenParser>();
        long payloadSeen = 0;
        long lastActivityTicks = Stopwatch.GetTimestamp();
        long firstPacketDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(8, warmupSeconds + durationSeconds + settleSeconds + 5)
            * Stopwatch.Frequency;
        long idleBreakTicks = (long)(Stopwatch.Frequency
            * (Math.Max(rcvTimeoutMs * 2, 1000) / 1000.0));

        using var poller = new Poller();
        poller.Add(server, PollEvents.PollIn);
        var events = new List<PollEvent>(1);
        int pendingCount = 0;
        Thread.Sleep(ReadyDelayMs);

        while (true)
        {
            int pollTimeoutMs = ComputeWaitTimeout(payloadSeen, lastActivityTicks,
                firstPacketDeadlineTicks, idleBreakTicks);
            if (pollTimeoutMs <= 0)
                break;

            poller.Modify(server, pendingCount > 0
                ? PollEvents.PollIn | PollEvents.PollOut
                : PollEvents.PollIn);
            if (!WaitForEvents(poller, events, pollTimeoutMs))
                continue;

            for (int i = 0; i < events.Count; i++)
            {
                PollEvent pollEvent = events[i];
                if ((pollEvent.Revents & PollEvents.PollOut) != 0)
                {
                    if (!FlushPendingMessages(server, pending,
                            ref pendingCount))
                    {
                        Console.Error.WriteLine(
                            "multi_server_error:stream_send_flush_failed");
                        return 2;
                    }
                }

                if ((pollEvent.Revents & PollEvents.PollIn) == 0)
                    continue;

                while (true)
                {
                    RelayStatus status = RelayStreamMessageNonBlocking(server,
                        pending, ref pendingCount, stopParsers,
                        ref payloadSeen, ref lastActivityTicks);
                    if (status == RelayStatus.Idle)
                        break;
                    if (status == RelayStatus.Stop)
                        return 0;
                    if (status == RelayStatus.Error)
                    {
                        Console.Error.WriteLine(
                            "multi_server_error:stream_recv_or_send_failed");
                        return 2;
                    }
                }
            }
        }

        return 0;
    }

    private static int ResolvePendingCapacity()
    {
        int clients = ResolveClients(Pattern);
        int hwm = ResolveHwm(Pattern);
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

    private static RelayStatus RelayStreamMessageNonBlocking(Zlink.Socket server,
        PendingStreamMessage[] pending, ref int pendingCount,
        Dictionary<RoutingKey, Len32StopTokenParser> stopParsers,
        ref long payloadSeen, ref long lastActivityTicks)
    {
        Message? idFrame = TryReceiveStreamFrame(server);
        if (idFrame == null)
            return RelayStatus.Idle;

        try
        {
            if (server.GetOption(SocketOptions.RcvMore) == 0)
                return RelayStatus.Error;

            ReadOnlySpan<byte> routingId = idFrame.AsReadOnlySpan();
            RoutingKey routingKey = RoutingKey.Create(routingId);

            while (true)
            {
                Message? payloadFrame = TryReceiveStreamFrame(server);
                if (payloadFrame == null)
                    return RelayStatus.Idle;

                bool more = server.GetOption(SocketOptions.RcvMore) != 0;
                try
                {
                    ReadOnlySpan<byte> payload = payloadFrame.AsReadOnlySpan();
                    if (IsStreamEventPayload(payload))
                    {
                        if (!more)
                            break;
                        continue;
                    }

                    payloadSeen++;
                    lastActivityTicks = Stopwatch.GetTimestamp();
                    if (IsStopTokenPayload(payload)
                        || ConsumeStopToken(stopParsers, routingKey, payload))
                    {
                        return RelayStatus.Stop;
                    }

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

    private static bool ConsumeStopToken(
        Dictionary<RoutingKey, Len32StopTokenParser> stopParsers,
        RoutingKey routingKey, ReadOnlySpan<byte> payload)
    {
        if (!stopParsers.TryGetValue(routingKey, out Len32StopTokenParser? parser))
        {
            parser = new Len32StopTokenParser();
            stopParsers[routingKey] = parser;
        }
        return parser.Consume(payload);
    }

    private static Message? TryReceiveStreamFrame(Zlink.Socket socket)
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

    private static bool FlushPendingMessages(Zlink.Socket server,
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

    private static SendStatus TrySendPendingMessage(Zlink.Socket server,
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
        return payload.Length == 1 && (payload[0] == 0x00 || payload[0] == 0x01);
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

    private readonly struct RoutingKey : IEquatable<RoutingKey>
    {
        private readonly uint _u32;
        private readonly string? _text;

        private RoutingKey(uint u32, string? text)
        {
            _u32 = u32;
            _text = text;
        }

        internal static RoutingKey Create(ReadOnlySpan<byte> routingId)
        {
            if (routingId.Length == sizeof(uint))
                return new RoutingKey(
                    BinaryPrimitives.ReadUInt32BigEndian(routingId), null);
            return new RoutingKey(0, Convert.ToHexString(routingId));
        }

        public bool Equals(RoutingKey other)
        {
            return _u32 == other._u32
                && string.Equals(_text, other._text, StringComparison.Ordinal);
        }

        public override bool Equals(object? obj)
        {
            return obj is RoutingKey other && Equals(other);
        }

        public override int GetHashCode()
        {
            return HashCode.Combine(_u32, _text);
        }
    }
}
