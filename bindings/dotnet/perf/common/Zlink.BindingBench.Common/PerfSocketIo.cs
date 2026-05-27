using System;
using Systems.Zlink;

public static class PerfSocketIo
{
    public static int Send(IMessageSocket socket, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        Message message = new Message(payload);
        try
        {
            if (socket.Send().Message(message).Flags(flags).Submit())
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

    public static int Send(IRoutedMessageSocket socket, string routingId,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        return Send(socket, RoutingId.From(System.Text.Encoding.UTF8.GetBytes(routingId)),
            payload, flags);
    }

    public static int Send(IRoutedMessageSocket socket, RoutingId routingId,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        Message message = new Message(payload);
        try
        {
            if (socket.Send(routingId).Message(message).Flags(flags).Submit())
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

    public static int Publish(IPublisherSocket socket, string topic,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        Message message = new Message(payload);
        try
        {
            if (socket.Publish(topic).Message(message).Flags(flags).Submit())
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

    public static int Publish(ISpot spot, string topic, ReadOnlySpan<byte> payload,
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
