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

    /// <summary>
    /// Start a send operation (operation builder).
    /// </summary>
    public SendOperation Send()
    {
        return new MessageSocketSendOperation(this);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool SendCore(Message message, SendFlags flags = SendFlags.None)
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
    internal bool SendCore(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts.Count == 1)
            return SendCore(parts[0], flags);
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

    /// <summary>
    /// Receive a message into <paramref name="result"/>. Caller-provided
    /// storage is the canonical recv shape; reuse the same Received
    /// instance across calls to avoid per-recv allocation. See
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
        return Kernel.ReceiveInto(result, (int)flags);
    }


    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
