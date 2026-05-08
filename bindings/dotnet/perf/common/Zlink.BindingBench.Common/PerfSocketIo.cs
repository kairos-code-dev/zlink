using System;
using Systems.Zlink;

public static class PerfSocketIo
{
    public static int Send(SocketBase socket, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        using Message message = Message.FromBytes(payload);
        bool sent = socket switch
        {
            MessageSocketBase messageSocket => messageSocket.Send(message, flags),
            _ => throw new NotSupportedException(
                $"Unsupported socket type for perf send: {socket.GetType().Name}")
        };

        if (!sent)
            ThrowWouldBlock();
        return payload.Length;
    }

    public static int Send(RoutedMessageSocketBase socket, string routingId,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        return Send(socket, RoutingId.FromBytes(System.Text.Encoding.UTF8.GetBytes(routingId)),
            payload, flags);
    }

    public static int Send(RoutedMessageSocketBase socket, RoutingId routingId,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        using Message message = Message.FromBytes(payload);
        if (!socket.Send(routingId, message, flags))
            ThrowWouldBlock();
        return payload.Length;
    }

    public static int Publish(PublisherSocketBase socket, string topic,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        using Message message = Message.FromBytes(payload);
        if (!socket.Publish(topic, message, flags))
            ThrowWouldBlock();
        return payload.Length;
    }

    public static int Publish(Spot spot, string serviceName, string topic,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        using Message message = Message.FromBytes(payload);
        if (!spot.Publish(serviceName, topic).Message(message).Flags(flags).Submit())
            ThrowWouldBlock();
        return payload.Length;
    }

    private static void ThrowWouldBlock()
    {
        throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.Backpressured,
            PerfShared.ErrnoEagain);
    }
}
