// SPDX-License-Identifier: MPL-2.0

using System.ComponentModel;
using Zlink.Sockets.Internal;

namespace Zlink;

[EditorBrowsable(EditorBrowsableState.Never)]
public abstract class SubscriberSocketBase : ConnectableSocketBase
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

    public TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None)
    {
        return (flags & RecvFlags.DontWait) != 0
            ? Kernel.SubscribeNoWait()
            : Kernel.Subscribe(flags);
    }

    internal bool SubscribeNoWait(out TopicMessage? subscribed)
    {
        subscribed = Kernel.SubscribeNoWait();
        return subscribed != null;
    }
}
