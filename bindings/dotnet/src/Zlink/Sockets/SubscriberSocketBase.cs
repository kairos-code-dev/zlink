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

    public Subscribed Subscribe()
    {
        return Kernel.Subscribe();
    }

    public Subscribed? TrySubscribe()
    {
        return Kernel.TrySubscribe();
    }

    public void SubscribeHandler(SocketSubscribeHandler handler)
    {
        Kernel.SubscribeHandler(handler);
    }
}
