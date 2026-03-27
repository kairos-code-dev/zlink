// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using Zlink.Sockets.Internal;

namespace Zlink;

public abstract class MessageSocketBase : SocketBase
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

    public void Send(string routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, message, flags);
    }

    public void Send(string routingId, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Send(routingId, parts, flags);
    }

    public void RecvHandler(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    public void Receive(out Message message, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out message, flags);
    }

    public void Receive(out Message[] parts, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out parts, flags);
    }

    public void Receive(out string routingId, out Message message,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out routingId, out message, flags);
    }

    public void Receive(out string routingId, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Receive(out routingId, out parts, flags);
    }
}
