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
}
