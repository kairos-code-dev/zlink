// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
public abstract class RoutedMessageSocketBase : SocketBase, IRoutedMessageSocket
{
    internal RoutedMessageSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal RoutedMessageSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    /// <summary>
    /// Start a routed send operation (operation builder).
    /// </summary>
    public SendOperation Send(RoutingId routingId)
    {
        return new RoutedSendOperation(this, routingId);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool SendStringCore(string routingId, Message message,
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool SendRoutedCore(RoutingId routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(
                Kernel.SendRoutedMessageResultUnchecked(routingId, message,
                    (int)flags));
        }

        Kernel.SendRoutedMessageUnchecked(routingId, message, flags);
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool SendStringCore(string routingId, IReadOnlyList<Message> parts,
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool SendRoutedCore(RoutingId routingId,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (parts.Count == 1)
            return SendRoutedCore(routingId, parts[0], flags);
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

    internal SendResult SendNoWaitResult(string routingId,
        IReadOnlyList<Message> parts)
    {
        return Kernel.SendNoWaitResult(routingId, parts);
    }

    internal SendResult SendNoWaitResult(RoutingId routingId,
        IReadOnlyList<Message> parts)
    {
        return Kernel.SendNoWaitResult(routingId, parts);
    }

    internal void OnReceive(SocketRecvHandler handler)
    {
        Kernel.RecvHandler(handler);
    }

    /// <summary>
    /// Receive a routed message into <paramref name="result"/>.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Recv(Received result, RecvFlags flags = RecvFlags.None)
    {
        return Kernel.ReceiveRoutedInto(result, (int)flags);
    }


    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
