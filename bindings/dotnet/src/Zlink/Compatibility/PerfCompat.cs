using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Zlink;

[Flags]
internal enum SendFlags
{
    None = 0,
    DontWait = 1,
    SendMore = 2
}

[Flags]
internal enum ReceiveFlags
{
    None = 0,
    DontWait = 1
}

internal static class PerfRawSocketCompat
{
    private const int BorrowedSendThreshold = 65536;

    internal sealed class SocketState
    {
        public readonly List<byte[]> PendingSendFrames = new();
        public readonly Queue<byte[]> PendingReceiveFrames = new();
    }

    private static readonly ConditionalWeakTable<Socket, SocketState> States =
        new();

    internal static bool TryGetInt32Option(Socket socket,
        SocketOptionKey<int> option, out int value)
    {
        if (option.Option == SocketOption.RcvMore)
        {
            value = GetState(socket).PendingReceiveFrames.Count > 0 ? 1 : 0;
            return true;
        }

        value = 0;
        return false;
    }

    internal static bool HasPendingSendFrames(Socket socket)
    {
        return GetState(socket).PendingSendFrames.Count > 0;
    }

    internal static bool TrySend(Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags, out int written)
    {
        SocketState state = GetState(socket);
        written = buffer.Length;
        byte[] frame = buffer.ToArray();

        if ((flags & SendFlags.SendMore) != 0)
        {
            state.PendingSendFrames.Add(frame);
            return true;
        }

        return FlushPendingFrames(socket, state, frame,
            (flags & SendFlags.DontWait) != 0);
    }

    internal static bool TryDequeueFrame(Socket socket, ReceiveFlags flags,
        out byte[]? frame)
    {
        SocketState state = GetState(socket);
        if (state.PendingReceiveFrames.Count == 0
            && !FillReceiveQueue(socket, state,
                (flags & ReceiveFlags.DontWait) != 0))
        {
            frame = null;
            return false;
        }

        if (state.PendingReceiveFrames.Count == 0)
        {
            frame = null;
            return false;
        }

        frame = state.PendingReceiveFrames.Dequeue();
        return true;
    }

    private static bool FillReceiveQueue(Socket socket, SocketState state,
        bool nonBlocking)
    {
        if (socket.Type == SocketType.Sub || socket.Type == SocketType.XSub)
        {
            byte[][]? subscribedFrames = socket.TryReceiveRawSubscribedFrames(
                nonBlocking ? 1 : 0);
            if (subscribedFrames == null || subscribedFrames.Length == 0)
                return false;

            for (int i = 0; i < subscribedFrames.Length; i++)
                state.PendingReceiveFrames.Enqueue(subscribedFrames[i]);
            return state.PendingReceiveFrames.Count > 0;
        }

        byte[][]? frames = socket.TryReceiveRawFrames(nonBlocking ? 1 : 0);
        if (frames == null || frames.Length == 0)
            return false;

        for (int i = 0; i < frames.Length; i++)
            state.PendingReceiveFrames.Enqueue(frames[i]);
        return state.PendingReceiveFrames.Count > 0;
    }

    private static bool FlushPendingFrames(Socket socket, SocketState state,
        byte[] finalFrame, bool nonBlocking)
    {
        if (finalFrame.Length > 0 || state.PendingSendFrames.Count == 0)
            state.PendingSendFrames.Add(finalFrame);

        try
        {
            if (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub)
            {
                using Message message = Message.FromBytes(finalFrame);
                SendResult result = socket.TryPublish(string.Empty, message);
                if (!nonBlocking && result != SendResult.Sent)
                {
                    throw new ZlinkException((int)ErrorCode.EAgain,
                        "send would block");
                }
                return result == SendResult.Sent;
            }

            if (RequiresRoutedSend(socket) && state.PendingSendFrames.Count > 0)
            {
                string routingId = RoutingIdCodec.ToPublicString(
                    state.PendingSendFrames[0]);
                Message[] payload = BuildPayload(state.PendingSendFrames, 1);
                try
                {
                    if (payload.Length == 0)
                        return true;

                    if (!nonBlocking)
                    {
                        if (payload.Length == 1)
                            socket.Send(routingId, payload[0]);
                        else
                            socket.Send(routingId, payload);
                        return true;
                    }

                    SendResult result = payload.Length == 1
                        ? socket.TrySend(routingId, payload[0])
                        : socket.TrySend(routingId, payload);
                    return result == SendResult.Sent;
                }
                finally
                {
                    DisposeMessages(payload);
                }
            }

            Message[] parts = BuildPayload(state.PendingSendFrames, 0);
            try
            {
                if (parts.Length == 0)
                    return true;

                if (!nonBlocking)
                {
                    if (parts.Length == 1)
                        socket.Send(parts[0]);
                    else
                        socket.Send(parts);
                    return true;
                }

                SendResult result = parts.Length == 1
                    ? socket.TrySend(parts[0])
                    : socket.TrySend(parts);
                return result == SendResult.Sent;
            }
            finally
            {
                DisposeMessages(parts);
            }
        }
        finally
        {
            state.PendingSendFrames.Clear();
        }
    }

    internal static SocketState GetState(Socket socket)
    {
        return States.GetValue(socket, _ => new SocketState());
    }

    private static bool RequiresRoutedSend(Socket socket)
    {
        return socket.Type == SocketType.Router || socket.Type == SocketType.Stream;
    }

    private static bool RequiresRoutedReceive(Socket socket)
    {
        return socket.Type == SocketType.Router || socket.Type == SocketType.Stream;
    }

    private static Message[] BuildPayload(List<byte[]> frames, int startIndex)
    {
        int count = Math.Max(0, frames.Count - startIndex);
        var payload = new Message[count];
        for (int i = 0; i < count; i++)
            payload[i] = Message.FromBytes(frames[startIndex + i]);
        return payload;
    }

    private static void DisposeMessages(IEnumerable<Message> messages)
    {
        foreach (Message message in messages)
            message.Dispose();
    }
}

internal static class PerfCompatExtensions
{
    private const int BorrowedSendThreshold = 65536;

    internal static SocketMonitor MonitorOpen(this Socket socket,
        SocketEvent events)
    {
        return socket.MonitorOpen(events);
    }

    internal static int Send(this Socket socket, byte[] buffer, SendFlags flags)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));

        bool nonBlocking = (flags & SendFlags.DontWait) != 0;
        bool multipart = (flags & SendFlags.SendMore) != 0;
        bool canBorrow = !multipart
            && !PerfRawSocketCompat.HasPendingSendFrames(socket)
            && buffer.Length >= BorrowedSendThreshold;

        if (canBorrow)
        {
            if (!nonBlocking)
            {
                if (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub)
                    socket.PublishBorrowedSingle(string.Empty, buffer, 0);
                else
                    socket.SendBorrowedSingle(buffer, 0);
                return buffer.Length;
            }

            if (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub)
            {
                SendResult pubResult = socket.TryPublishBorrowedSingle(
                    string.Empty, buffer);
                if (pubResult == SendResult.Sent)
                    return buffer.Length;
                throw new ZlinkException((int)ErrorCode.EAgain, "send would block");
            }

            SendResult result = socket.TrySendBorrowedSingle(buffer);
            if (result == SendResult.Sent)
                return buffer.Length;
            throw new ZlinkException((int)ErrorCode.EAgain, "send would block");
        }

        return Send(socket, buffer.AsSpan(), flags);
    }

    internal static int Send(this Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags)
    {
        bool nonBlocking = (flags & SendFlags.DontWait) != 0;
        bool multipart = (flags & SendFlags.SendMore) != 0;

        if (!nonBlocking && !multipart
            && !PerfRawSocketCompat.HasPendingSendFrames(socket))
        {
            if (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub)
            {
                socket.PublishRawSingle(string.Empty, buffer, 0);
                return buffer.Length;
            }

            socket.SendRawSingle(buffer, 0);
            return buffer.Length;
        }

        if (!multipart
            && !PerfRawSocketCompat.HasPendingSendFrames(socket)
            && (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub))
        {
            SendResult result = socket.TryPublishRawSingle(string.Empty, buffer);
            if (result != SendResult.Sent)
                throw new ZlinkException((int)ErrorCode.EAgain, "send would block");
            return buffer.Length;
        }

        if (!PerfRawSocketCompat.TrySend(socket, buffer, flags, out int written))
        {
            throw new ZlinkException((int)ErrorCode.EAgain, "send would block");
        }
        return written;
    }

    internal static bool TrySend(this Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags, out int written)
    {
        return PerfRawSocketCompat.TrySend(socket, buffer, flags, out written);
    }

    internal static int Receive(this Socket socket, Span<byte> buffer,
        ReceiveFlags flags)
    {
        if (!TryReceive(socket, buffer, flags, out int bytesWritten))
        {
            throw new ZlinkException((int)ErrorCode.EAgain, "receive would block");
        }
        return bytesWritten;
    }

    internal static bool TryReceive(this Socket socket, Span<byte> buffer,
        ReceiveFlags flags, out int read)
    {
        PerfRawSocketCompat.SocketState state =
            PerfRawSocketCompat.GetState(socket);
        if (state.PendingReceiveFrames.Count == 0)
        {
            if (socket.Type == SocketType.Sub || socket.Type == SocketType.XSub)
            {
                int? directRead = socket.TryReceiveRawSubscribedFrame(buffer,
                    (int)flags, out byte[][] pendingFrames);
                if (directRead.HasValue)
                {
                    for (int i = 0; i < pendingFrames.Length; i++)
                        state.PendingReceiveFrames.Enqueue(pendingFrames[i]);
                    read = directRead.Value;
                    return true;
                }
            }
            else if (socket.Type != SocketType.Router
                && socket.Type != SocketType.Stream)
            {
                int? directRead = socket.TryReceiveRawFrame(buffer, (int)flags,
                    out byte[][] pendingFrames);
                if (directRead.HasValue)
                {
                    for (int i = 0; i < pendingFrames.Length; i++)
                        state.PendingReceiveFrames.Enqueue(pendingFrames[i]);
                    read = directRead.Value;
                    return true;
                }
            }
        }

        if (!PerfRawSocketCompat.TryDequeueFrame(socket, flags, out byte[]? frame))
        {
            read = 0;
            return false;
        }

        if (frame!.Length > buffer.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(buffer));
        }

        frame.CopyTo(buffer);
        read = frame.Length;
        return true;
    }

    internal static Message ReceiveMessage(this Socket socket, ReceiveFlags flags)
    {
        if (PerfRawSocketCompat.TryDequeueFrame(socket, flags, out byte[]? frame))
            return Message.FromBytes(frame!);

        throw new ZlinkException((int)ErrorCode.EAgain, "receive would block");
    }

    internal static SocketMonitorEvent Receive(this SocketMonitor monitor,
        ReceiveFlags flags)
    {
        if ((flags & ReceiveFlags.DontWait) != 0)
        {
            if (monitor.TryRecv(out SocketMonitorEvent? evt))
                return evt!.Value;
            throw new ZlinkException((int)ErrorCode.EAgain,
                "monitor receive would block");
        }

        return monitor.Recv();
    }

    internal static int StreamSend(this Socket socket, string routingId,
        ReadOnlySpan<byte> payload, SendFlags flags)
    {
        using Message message = Message.FromBytes(payload);
        SendResult result = socket.TrySend(routingId, message);
        if (result == SendResult.Sent)
            return payload.Length;

        throw new ZlinkException((int)ErrorCode.EAgain, "send would block");
    }

    internal static PeerRecord[] GetPeers(this Socket socket)
    {
        return Array.Empty<PeerRecord>();
    }
}
