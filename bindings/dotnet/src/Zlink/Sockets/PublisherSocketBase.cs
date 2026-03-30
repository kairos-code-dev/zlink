// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;
using Zlink.Sockets.Internal;

namespace Zlink;

public abstract class PublisherSocketBase : SocketBase
{
    internal PublisherSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal PublisherSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    public void Publish(string topic, Message message)
    {
        Kernel.Publish(topic, message);
    }

    public void Publish(string topic, IReadOnlyList<Message> parts)
    {
        Kernel.Publish(topic, parts);
    }

    public SendResult TryPublish(string topic, Message message)
    {
        return Kernel.TryPublish(topic, message);
    }

    public SendResult TryPublish(string topic, IReadOnlyList<Message> parts)
    {
        return Kernel.TryPublish(topic, parts);
    }
}
