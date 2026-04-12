using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Zlink;

[Flags]
internal enum PerfSendFlags
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

    private static readonly ConditionalWeakTable<SocketBase, SocketState> States =
        new();

    internal static bool TryGetInt32Option(SocketBase socket,
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

    internal static bool HasPendingSendFrames(SocketBase socket)
    {
        return GetState(socket).PendingSendFrames.Count > 0;
    }

    internal static bool TrySend(SocketBase socket, ReadOnlySpan<byte> buffer,
        PerfSendFlags flags, out int written)
    {
        SocketState state = GetState(socket);
        written = buffer.Length;
        byte[] frame = buffer.ToArray();

        if ((flags & PerfSendFlags.SendMore) != 0)
        {
            state.PendingSendFrames.Add(frame);
            return true;
        }

        return FlushPendingFrames(socket, state, frame,
            (flags & PerfSendFlags.DontWait) != 0);
    }

    internal static bool TryDequeueFrame(SocketBase socket, ReceiveFlags flags,
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

    private static bool FillReceiveQueue(SocketBase socket, SocketState state,
        bool nonBlocking)
    {
        SocketType type = socket.Kernel.Type;
        if (type == SocketType.Sub || type == SocketType.XSub)
        {
            byte[][]? subscribedFrames = socket.Kernel.TryReceiveRawSubscribedFrames(
                nonBlocking ? 1 : 0);
            if (subscribedFrames == null || subscribedFrames.Length == 0)
                return false;

            for (int i = 0; i < subscribedFrames.Length; i++)
                state.PendingReceiveFrames.Enqueue(subscribedFrames[i]);
            return state.PendingReceiveFrames.Count > 0;
        }

        byte[][]? frames = socket.Kernel.TryReceiveRawFrames(nonBlocking ? 1 : 0);
        if (frames == null || frames.Length == 0)
            return false;

        for (int i = 0; i < frames.Length; i++)
            state.PendingReceiveFrames.Enqueue(frames[i]);
        return state.PendingReceiveFrames.Count > 0;
    }

    private static bool FlushPendingFrames(SocketBase socket, SocketState state,
        byte[] finalFrame, bool nonBlocking)
    {
        if (finalFrame.Length > 0 || state.PendingSendFrames.Count == 0)
            state.PendingSendFrames.Add(finalFrame);

        try
        {
            SocketType type = socket.Kernel.Type;
            if (type == SocketType.Pub || type == SocketType.XPub)
            {
                using Message message = Message.FromBytes(finalFrame);
                PublisherSocketBase publisher = (PublisherSocketBase)socket;
                SendResult result = publisher.TryPublish(string.Empty, message);
                if (!nonBlocking && result != SendResult.Sent)
                {
                    throw new ZlinkSubmitException(SubmitResult.Backpressured,
                        (int)ErrorCode.EAgain);
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
                        RoutedMessageSocketBase blockingRoutedSocket =
                            (RoutedMessageSocketBase)socket;
                        if (payload.Length == 1)
                            blockingRoutedSocket.Send(routingId, payload[0]);
                        else
                            blockingRoutedSocket.Send(routingId, payload);
                        return true;
                    }

                    RoutedMessageSocketBase nonBlockingRoutedSocket =
                        (RoutedMessageSocketBase)socket;
                    SendResult result = payload.Length == 1
                        ? nonBlockingRoutedSocket.TrySend(routingId, payload[0])
                        : nonBlockingRoutedSocket.TrySend(routingId, payload);
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
                    MessageSocketBase blockingMessageSocket =
                        (MessageSocketBase)socket;
                    if (parts.Length == 1)
                        blockingMessageSocket.Send(parts[0]);
                    else
                        blockingMessageSocket.Send(parts);
                    return true;
                }

                MessageSocketBase nonBlockingMessageSocket =
                    (MessageSocketBase)socket;
                SendResult result = parts.Length == 1
                    ? nonBlockingMessageSocket.TrySend(parts[0])
                    : nonBlockingMessageSocket.TrySend(parts);
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

    internal static SocketState GetState(SocketBase socket)
    {
        return States.GetValue(socket, _ => new SocketState());
    }

    private static bool RequiresRoutedSend(SocketBase socket)
    {
        SocketType type = socket.Kernel.Type;
        return type == SocketType.Router || type == SocketType.Stream;
    }

    private static bool RequiresRoutedReceive(SocketBase socket)
    {
        SocketType type = socket.Kernel.Type;
        return type == SocketType.Router || type == SocketType.Stream;
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

    internal static int Send(this SocketBase socket, byte[] buffer, PerfSendFlags flags)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));

        bool nonBlocking = (flags & PerfSendFlags.DontWait) != 0;
        bool multipart = (flags & PerfSendFlags.SendMore) != 0;
        SocketType type = socket.Kernel.Type;
        bool canBorrow = !multipart
            && !PerfRawSocketCompat.HasPendingSendFrames(socket)
            && buffer.Length >= BorrowedSendThreshold;

        if (canBorrow)
        {
            if (!nonBlocking)
            {
                if (type == SocketType.Pub || type == SocketType.XPub)
                    socket.Kernel.PublishBorrowedSingle(string.Empty, buffer, 0);
                else
                    socket.Kernel.SendBorrowedSingle(buffer, 0);
                return buffer.Length;
            }

            if (type == SocketType.Pub || type == SocketType.XPub)
            {
                SendResult pubResult = socket.Kernel.TryPublishBorrowedSingle(
                    string.Empty, buffer);
                if (pubResult == SendResult.Sent)
                    return buffer.Length;
                throw new ZlinkSubmitException(SubmitResult.Backpressured,
                    (int)ErrorCode.EAgain);
            }

            SendResult result = socket.Kernel.TrySendBorrowedSingle(buffer);
            if (result == SendResult.Sent)
                return buffer.Length;
            throw new ZlinkSubmitException(SubmitResult.Backpressured,
                (int)ErrorCode.EAgain);
        }

        return Send(socket, buffer.AsSpan(), flags);
    }

    internal static int Send(this SocketBase socket, ReadOnlySpan<byte> buffer,
        PerfSendFlags flags)
    {
        bool nonBlocking = (flags & PerfSendFlags.DontWait) != 0;
        bool multipart = (flags & PerfSendFlags.SendMore) != 0;
        SocketType type = socket.Kernel.Type;

        if (!nonBlocking && !multipart
            && !PerfRawSocketCompat.HasPendingSendFrames(socket))
        {
            if (type == SocketType.Pub || type == SocketType.XPub)
            {
                socket.Kernel.PublishRawSingle(string.Empty, buffer, 0);
                return buffer.Length;
            }

            socket.Kernel.SendRawSingle(buffer, 0);
            return buffer.Length;
        }

        if (!multipart
            && !PerfRawSocketCompat.HasPendingSendFrames(socket)
            && (type == SocketType.Pub || type == SocketType.XPub))
        {
            SendResult result = socket.Kernel.TryPublishRawSingle(string.Empty, buffer);
            if (result != SendResult.Sent)
                throw new ZlinkSubmitException(SubmitResult.Backpressured,
                    (int)ErrorCode.EAgain);
            return buffer.Length;
        }

        if (!PerfRawSocketCompat.TrySend(socket, buffer, flags, out int written))
        {
            throw new ZlinkSubmitException(SubmitResult.Backpressured,
                (int)ErrorCode.EAgain);
        }
        return written;
    }

    internal static bool TrySend(this SocketBase socket, ReadOnlySpan<byte> buffer,
        PerfSendFlags flags, out int written)
    {
        return PerfRawSocketCompat.TrySend(socket, buffer, flags, out written);
    }

    internal static int Receive(this SocketBase socket, Span<byte> buffer,
        ReceiveFlags flags)
    {
        if (!TryReceive(socket, buffer, flags, out int bytesWritten))
        {
            throw new ZlinkRecvException(RecvResult.NoData,
                (int)ErrorCode.EAgain);
        }
        return bytesWritten;
    }

    internal static bool TryReceive(this SocketBase socket, Span<byte> buffer,
        ReceiveFlags flags, out int read)
    {
        PerfRawSocketCompat.SocketState state =
            PerfRawSocketCompat.GetState(socket);
        if (state.PendingReceiveFrames.Count == 0)
        {
            SocketType type = socket.Kernel.Type;
            if (type == SocketType.Sub || type == SocketType.XSub)
            {
                int? directRead = socket.Kernel.TryReceiveRawSubscribedFrame(
                    buffer, (int)flags, out byte[][] pendingFrames);
                if (directRead.HasValue)
                {
                    for (int i = 0; i < pendingFrames.Length; i++)
                        state.PendingReceiveFrames.Enqueue(pendingFrames[i]);
                    read = directRead.Value;
                    return true;
                }
            }
            else if (type != SocketType.Router
                && type != SocketType.Stream)
            {
                int? directRead = socket.Kernel.TryReceiveRawFrame(buffer,
                    (int)flags, out byte[][] pendingFrames);
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

    internal static Message ReceiveMessage(this SocketBase socket,
        ReceiveFlags flags)
    {
        if (PerfRawSocketCompat.TryDequeueFrame(socket, flags, out byte[]? frame))
            return Message.FromBytes(frame!);

        throw new ZlinkRecvException(RecvResult.NoData,
            (int)ErrorCode.EAgain);
    }

    internal static SocketMonitorEvent Receive(this SocketMonitor monitor,
        ReceiveFlags flags)
    {
        if ((flags & ReceiveFlags.DontWait) != 0)
        {
            if (monitor.TryRecv(out SocketMonitorEvent? evt))
                return evt!.Value;
            throw new ZlinkRecvException(RecvResult.NoData,
                (int)ErrorCode.EAgain);
        }

        return monitor.Recv();
    }

    internal static int StreamSend(this SocketBase socket, string routingId,
        ReadOnlySpan<byte> payload, PerfSendFlags flags)
    {
        using Message message = Message.FromBytes(payload);
        SendResult result = ((RoutedMessageSocketBase)socket).TrySend(routingId,
            message);
        if (result == SendResult.Sent)
            return payload.Length;

        throw new ZlinkSubmitException(SubmitResult.Backpressured,
            (int)ErrorCode.EAgain);
    }

    internal static void SendBorrowedSingle(this SocketBase socket,
        string routingId, byte[] payload, int flags)
    {
        socket.Kernel.SendBorrowedSingle(routingId, payload, flags);
    }

    internal static PeerRecord[] GetPeers(this SocketBase socket)
    {
        return Array.Empty<PeerRecord>();
    }

    internal static Received? TryReceiveRouted(this SocketBase socket)
    {
        return socket.Kernel.TryReceiveRouted();
    }

    internal static int? TryReceiveRawRoutedFrame(this SocketBase socket,
        Span<byte> routingDestination, Span<byte> payloadDestination, int flags,
        out byte[][] pendingFrames)
    {
        return socket.Kernel.TryReceiveRawRoutedFrame(routingDestination,
            payloadDestination, flags, out pendingFrames);
    }

    internal static int? TryReceiveRawSubscribedFrame(this SocketBase socket,
        Span<byte> destination, int flags, out byte[][] pendingFrames)
    {
        return socket.Kernel.TryReceiveRawSubscribedFrame(destination, flags,
            out pendingFrames);
    }
}
