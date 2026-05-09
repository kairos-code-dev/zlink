// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Send(Message message, SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(
                Kernel.SendMessageResultUnchecked(message, (int)flags));
        }

        Kernel.SendMessageUnchecked(message, flags);
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Send(IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
            return SocketKernel.TrySendOrThrow(Kernel.SendNoWaitResult(parts));

        Kernel.Send(parts, flags);
        return true;
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        return (flags & RecvFlags.DontWait) != 0
            ? Kernel.RecvMessageNoWaitUnchecked()
            : Kernel.RecvMessageUnchecked(flags);
    }

    internal Received? RecvNoWait()
    {
        return Kernel.RecvMessageNoWaitUnchecked();
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
