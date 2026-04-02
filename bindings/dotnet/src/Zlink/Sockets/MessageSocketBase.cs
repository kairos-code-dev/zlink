// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using Zlink.Sockets.Internal;

namespace Zlink;

public abstract class MessageSocketBase : ConnectableSocketBase
{
    internal MessageSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal MessageSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    public void Send(Message message)
    {
        Kernel.Send(message);
    }

    public void Send(IReadOnlyList<Message> parts)
    {
        Kernel.Send(parts);
    }

    public SendResult TrySend(Message message)
    {
        return Kernel.TrySend(message);
    }

    public SendResult TrySend(IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(parts);
    }

    public void OnReceive(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public Received Recv()
    {
        return Kernel.Recv();
    }

    public bool TryRecv(out Received? received)
    {
        received = Kernel.TryRecv();
        return received != null;
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
