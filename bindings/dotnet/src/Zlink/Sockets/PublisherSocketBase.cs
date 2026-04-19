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

    public void Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Publish(topic, message, flags);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        Kernel.Publish(topic, parts, flags);
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
