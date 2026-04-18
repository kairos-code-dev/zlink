// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using Zlink.Sockets.Internal;

namespace Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
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

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        Kernel.Send(message, flags);
    }

    public void Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        Kernel.Send(parts, flags);
    }

    public bool TrySend(Message message)
    {
        return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(message));
    }

    public bool TrySend(IReadOnlyList<Message> parts)
    {
        return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(parts));
    }

    internal SendResult SendNoWaitResult(Message message)
    {
        return Kernel.SendNoWaitResult(message);
    }

    internal SendResult SendNoWaitResult(IReadOnlyList<Message> parts)
    {
        return Kernel.SendNoWaitResult(parts);
    }

    internal void OnReceive(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        return Kernel.Recv(flags);
    }

    public bool TryRecv(out Received? received)
    {
        received = Kernel.RecvNoWait();
        return received != null;
    }

    internal Received? RecvNoWait()
    {
        return Kernel.RecvNoWait();
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
