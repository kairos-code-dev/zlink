// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using Zlink.Sockets.Internal;

namespace Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
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

    public void Send(string routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, message, flags);
    }

    public void Send(RoutingId routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, message, flags);
    }

    public void Send(string routingId, byte[] payload,
        SendFlags flags = SendFlags.None)
    {
        Kernel.SendBorrowedSingle(routingId, payload, (int)flags);
    }

    public void Send(RoutingId routingId, byte[] payload,
        SendFlags flags = SendFlags.None)
    {
        Kernel.SendBorrowedSingle(routingId, payload, (int)flags);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, parts, flags);
    }

    public void Send(RoutingId routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, parts, flags);
    }

    internal SendResult TrySend(string routingId, Message message)
    {
        return Kernel.TrySend(routingId, message);
    }

    internal SendResult TrySend(RoutingId routingId, Message message)
    {
        return Kernel.TrySend(routingId, message);
    }

    internal SendResult TrySend(string routingId, byte[] payload)
    {
        return Kernel.TrySendBorrowedSingle(routingId, payload);
    }

    internal SendResult TrySend(RoutingId routingId, byte[] payload)
    {
        return Kernel.TrySendBorrowedSingle(routingId, payload);
    }

    internal SendResult TrySend(string routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(routingId, parts);
    }

    internal SendResult TrySend(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(routingId, parts);
    }

    internal void OnReceive(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        return Kernel.ReceiveRouted(flags);
    }

    internal bool TryRecv(out Received? received)
    {
        received = Kernel.TryReceiveRouted();
        return received != null;
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
