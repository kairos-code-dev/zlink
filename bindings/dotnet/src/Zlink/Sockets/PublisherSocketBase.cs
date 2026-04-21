// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using System.ComponentModel;
using Zlink.Sockets.Internal;

namespace Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
public abstract class PublisherSocketBase : ConnectableSocketBase
{
    internal PublisherSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal PublisherSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    public bool Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(Kernel.PublishNoWaitResult(topic,
                message));
        }

        Kernel.Publish(topic, message, flags);
        return true;
    }

    public bool Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(Kernel.PublishNoWaitResult(topic,
                parts));
        }

        Kernel.Publish(topic, parts, flags);
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic, Message message)
    {
        return Kernel.PublishNoWaitResult(topic, message);
    }

    internal SendResult PublishNoWaitResult(string topic, IReadOnlyList<Message> parts)
    {
        return Kernel.PublishNoWaitResult(topic, parts);
    }

    public void OnSendReady(Action handler)
    {
        Kernel.SendReadyHandler(handler);
    }
}
