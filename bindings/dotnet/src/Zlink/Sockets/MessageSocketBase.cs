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

    internal SendResult TrySend(Message message)
    {
        return Kernel.TrySend(message);
    }

    internal SendResult TrySend(IReadOnlyList<Message> parts)
    {
        return Kernel.TrySend(parts);
    }

    public void OnReceive(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public Received Recv(RecvFlags flags = RecvFlags.None)
    {
        return Kernel.Recv(flags);
    }

    internal bool TryRecv(out Received? received)
    {
        received = Kernel.TryRecv();
        return received != null;
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
