using System;
using Systems.Zlink;

public static class PerfSocketIo
{
    public static int Send(SocketBase socket, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        Message message = new Message(payload);
        try
        {
            bool sent = socket switch
            {
                MessageSocketBase messageSocket => messageSocket.Send(message, flags),
                _ => throw new NotSupportedException(
                    $"Unsupported socket type for perf send: {socket.GetType().Name}")
            };

            if (sent)
                return payload.Length;
            message.Dispose();
            return 0;
        }
        catch
        {
            message.Dispose();
            throw;
        }
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
        Message message = new Message(payload);
        try
        {
            if (socket.Send(routingId, message, flags))
                return payload.Length;
            message.Dispose();
            return 0;
        }
        catch
        {
            message.Dispose();
            throw;
        }
    }

    public static int Publish(PublisherSocketBase socket, string topic,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        Message message = new Message(payload);
        try
        {
            if (socket.Publish(topic, message, flags))
                return payload.Length;
            message.Dispose();
            return 0;
        }
        catch
        {
            message.Dispose();
            throw;
        }
    }

    public static int Publish(Spot spot, string topic, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        Message message = new Message(payload);
        try
        {
            if (spot.Publish(topic).Message(message).Flags(flags).Submit())
                return payload.Length;
            message.Dispose();
            return 0;
        }
        catch
        {
            message.Dispose();
            throw;
        }
    }
}
