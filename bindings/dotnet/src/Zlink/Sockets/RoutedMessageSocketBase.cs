// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

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

    public bool Send(string routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
                message));
        }

        Kernel.Send(routingId, message, flags);
        return true;
    }

    public bool Send(RoutingId routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
                message));
        }

        Kernel.Send(routingId, message, flags);
        return true;
    }

    public bool Send(string routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
                parts));
        }

        Kernel.Send(routingId, parts, flags);
        return true;
    }

    public bool Send(RoutingId routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(routingId,
                parts));
        }

        Kernel.Send(routingId, parts, flags);
        return true;
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

    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        return (flags & RecvFlags.DontWait) != 0
            ? Kernel.ReceiveRoutedNoWait()
            : Kernel.ReceiveRouted(flags);
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
