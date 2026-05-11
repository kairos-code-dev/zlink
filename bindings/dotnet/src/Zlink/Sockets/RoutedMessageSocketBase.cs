// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool Send(string routingId, Message message,
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
    public bool Send(RoutingId routingId, Message message,
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
    internal bool Send(string routingId, IReadOnlyList<Message> parts,
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

    /// <summary>
    /// Receive a routed message into <paramref name="result"/>. Caller-provided
    /// storage is the canonical recv shape; reuse the same Received instance
    /// across calls to avoid per-recv allocation. See
    /// doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
    /// </summary>
    /// <param name="result">Long-lived Received storage. Internal state is
    /// reset and refilled on each successful call.</param>
    /// <param name="flags">Recv flags. With <see cref="RecvFlags.DontWait"/>,
    /// returns <c>false</c> when no data is available; otherwise blocks
    /// until data arrives or the socket reports a hard error.</param>
    /// <returns>true on success, false when DontWait is set and no data is
    /// available.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Recv(Received result, RecvFlags flags = RecvFlags.None)
    {
        return Kernel.ReceiveRoutedInto(result, (int)flags);
    }

    /// <summary>
    /// Legacy convenience overload that allocates a fresh
    /// <see cref="Received"/> per call. Prefer
    /// <see cref="Recv(Received, RecvFlags)"/> in any hot path. This overload
    /// exists to keep existing call sites compiling during the migration
    /// to the canonical caller-provided-storage recv shape.
    /// </summary>
    [Obsolete("Use Recv(Received result, RecvFlags) — pass caller-provided storage to avoid the per-recv allocation.")]
    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        var result = new Received();
        if (Recv(result, flags))
            return result;
        result.Dispose();
        return null;
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
