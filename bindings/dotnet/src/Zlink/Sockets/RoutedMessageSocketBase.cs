// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using Zlink.Sockets.Internal;

namespace Zlink;

public abstract class RoutedMessageSocketBase : SocketBase
{
    internal RoutedMessageSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal RoutedMessageSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    public void Send(string routingId, Message message)
    {
        Kernel.Send(routingId, message);
    }

    public void Send(RoutingId routingId, Message message)
    {
        Kernel.Send(routingId.Value, message);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts)
    {
        Kernel.Send(routingId, parts);
    }

    public void Send(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        Kernel.Send(routingId.Value, parts);
    }

    public SendResult TrySend(string routingId, Message message)
    {
        return Kernel.TrySend(routingId, message);
    }

    public SendResult TrySend(RoutingId routingId, Message message)
    {
        return Kernel.TrySend(routingId.Value, message);
    }

    public SendResult TrySend(string routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(routingId, parts);
    }

    public SendResult TrySend(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(routingId.Value, parts);
    }

    public void RecvHandler(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public Received Receive()
    {
        return Kernel.ReceiveRouted();
    }

    public Received? TryReceive()
    {
        return Kernel.TryReceiveRouted();
    }
}
