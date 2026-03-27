// SPDX-License-Identifier: MPL-2.0

using Zlink.Sockets.Internal;

namespace Zlink;

public abstract class SubscriberSocketBase : SocketBase
{
    internal SubscriberSocketBase(Context context, SocketType type)
        : base(context, type)
    {
    }

    internal SubscriberSocketBase(SocketKernel kernel)
        : base(kernel)
    {
    }

    public void SetSubscription(string topicOrPattern)
    {
        Kernel.SetSubscription(topicOrPattern);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        Kernel.UnsetSubscription(topicOrPattern);
    }

    public void Subscribe(out string topic, out Message message,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out topic, out message, flags);
    }

    public void Subscribe(out string topic, out Message[] parts,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out topic, out parts, flags);
    }

    public void Subscribe(out string routingId, out string topic,
        out Message message, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out routingId, out topic, out message, flags);
    }

    public void Subscribe(out string routingId, out string topic,
        out Message[] parts, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.Subscribe(out routingId, out topic, out parts, flags);
    }

    public void SubscribeHandler(SocketSubscribeHandler handler)
    {
        Kernel.SubscribeHandler(handler);
    }
}
