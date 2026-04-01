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
    private sealed class SocketState
    {
        public readonly List<byte[]> PendingSendFrames = new();
        public readonly Queue<Message> PendingReceiveFrames = new();
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

    internal static bool TryDequeueMessage(Socket socket, ReceiveFlags flags,
        out Message? message)
    {
        SocketState state = GetState(socket);
        if (state.PendingReceiveFrames.Count == 0
            && !FillReceiveQueue(socket, state,
                (flags & ReceiveFlags.DontWait) != 0))
        {
            message = null;
            return false;
        }

        if (state.PendingReceiveFrames.Count == 0)
        {
            message = null;
            return false;
        }

        message = state.PendingReceiveFrames.Dequeue();
        return true;
    }

    private static bool FillReceiveQueue(Socket socket, SocketState state,
        bool nonBlocking)
    {
        if (socket.Type == SocketType.Sub || socket.Type == SocketType.XSub)
        {
            Subscribed? subscribed = nonBlocking
                ? socket.TrySubscribe()
                : socket.Subscribe();
            if (subscribed == null)
                return false;

            for (int i = 0; i < subscribed.Parts.Count; i++)
                state.PendingReceiveFrames.Enqueue((Message)subscribed.Parts[i]);
            return state.PendingReceiveFrames.Count > 0;
        }

        Received? received = RequiresRoutedReceive(socket)
            ? (nonBlocking ? socket.TryReceiveRouted() : socket.ReceiveRouted())
            : (nonBlocking ? socket.TryReceive() : socket.Receive());
        if (received == null)
            return false;

        if (!string.IsNullOrEmpty(received.RoutingId))
        {
            state.PendingReceiveFrames.Enqueue(Message.FromBytes(
                RoutingIdCodec.FromPublicString(received.RoutingId,
                    nameof(received.RoutingId))));
        }

        for (int i = 0; i < received.Parts.Count; i++)
            state.PendingReceiveFrames.Enqueue((Message)received.Parts[i]);
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

                    SendResult result = payload.Length == 1
                        ? socket.TrySend(routingId, payload[0])
                        : socket.TrySend(routingId, payload);
                    if (!nonBlocking && result != SendResult.Sent)
                        throw new ZlinkException((int)ErrorCode.EAgain,
                            "send would block");
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

                SendResult result = parts.Length == 1
                    ? socket.TrySend(parts[0])
                    : socket.TrySend(parts);
                if (!nonBlocking && result != SendResult.Sent)
                    throw new ZlinkException((int)ErrorCode.EAgain,
                        "send would block");
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

    private static SocketState GetState(Socket socket)
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
    internal static SocketMonitor MonitorOpen(this Socket socket,
        SocketEvent events)
    {
        return socket.OpenMonitor(events);
    }

    internal static int Send(this Socket socket, ReadOnlySpan<byte> buffer,
        SendFlags flags)
    {
        bool nonBlocking = (flags & SendFlags.DontWait) != 0;
        bool multipart = (flags & SendFlags.SendMore) != 0;

        if (!nonBlocking && !multipart)
        {
            using Message message = Message.FromBytes(buffer);
            if (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub)
            {
                socket.Publish(string.Empty, message);
                return buffer.Length;
            }

            socket.Send(message);
            return buffer.Length;
        }

        if (!multipart
            && (socket.Type == SocketType.Pub || socket.Type == SocketType.XPub))
        {
            using Message message = Message.FromBytes(buffer);
            SendResult result = socket.TryPublish(string.Empty, message);
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
        using Message message = ReceiveMessage(socket, flags);
        if (!message.TryCopyTo(buffer, out int bytesWritten))
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(buffer));
        }
        return bytesWritten;
    }

    internal static bool TryReceive(this Socket socket, Span<byte> buffer,
        ReceiveFlags flags, out int read)
    {
        if (!PerfRawSocketCompat.TryDequeueMessage(socket, flags, out Message? message))
        {
            read = 0;
            return false;
        }

        using (message)
        {
            if (!message!.TryCopyTo(buffer, out read))
            {
                throw new ArgumentException("Destination buffer is too small.",
                    nameof(buffer));
            }
            return true;
        }
    }

    internal static Message ReceiveMessage(this Socket socket, ReceiveFlags flags)
    {
        if (PerfRawSocketCompat.TryDequeueMessage(socket, flags, out Message? message))
            return message!;

        throw new ZlinkException((int)ErrorCode.EAgain, "receive would block");
    }

    internal static SocketMonitorEvent Receive(this SocketMonitor monitor,
        ReceiveFlags flags)
    {
        if ((flags & ReceiveFlags.DontWait) != 0)
        {
            SocketMonitorEvent? evt = monitor.TryReceive();
            if (evt.HasValue)
                return evt.Value;
            throw new ZlinkException((int)ErrorCode.EAgain,
                "monitor receive would block");
        }

        return monitor.Receive();
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
