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

    public bool TrySend(string routingId, Message message)
    {
        return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
            message));
    }

    public bool TrySend(RoutingId routingId, Message message)
    {
        return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
            message));
    }

    public bool TrySend(string routingId, IReadOnlyList<Message> parts)
    {
        return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
            parts));
    }

    public bool TrySend(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
            parts));
    }

    internal SendResult SendNoWaitResult(string routingId, Message message)
    {
        return Kernel.SendNoWaitResult(routingId, message);
    }

    internal SendResult SendNoWaitResult(RoutingId routingId, Message message)
    {
        return Kernel.SendNoWaitResult(routingId, message);
    }

    internal SendResult SendNoWaitResult(string routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.SendNoWaitResult(routingId, parts);
    }

    internal SendResult SendNoWaitResult(RoutingId routingId, IReadOnlyList<Message> parts)
    {
        return Kernel.SendNoWaitResult(routingId, parts);
    }

    internal void OnReceive(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        return Kernel.ReceiveRouted(flags);
    }

    public bool TryRecv(out Received? received)
    {
        received = Kernel.ReceiveRoutedNoWait();
        return received != null;
    }

    internal Received? RecvNoWait()
    {
        return Kernel.ReceiveRoutedNoWait();
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
