// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed class XPubSocket : PublisherSocketBase
{
    public new PubSocketOptions Options { get; }

    public XPubSocket(Context context)
        : base(context, SocketType.XPub)
    {
        Options = new PubSocketOptions(this);
    }

    public SubscriptionEvent? ReceiveSubscriptionEvent(
        RecvFlags flags = RecvFlags.None)
    {
        return (flags & RecvFlags.DontWait) != 0
            ? Kernel.ReceiveSubscriptionEventNoWait()
            : Kernel.ReceiveSubscriptionEvent(flags);
    }

    internal bool ReceiveSubscriptionEventNoWait(out SubscriptionEvent? subscriptionEvent)
    {
        subscriptionEvent = Kernel.ReceiveSubscriptionEventNoWait();
        return subscriptionEvent != null;
    }
}
