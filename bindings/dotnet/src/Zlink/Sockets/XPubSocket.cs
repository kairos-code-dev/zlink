// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed class XPubSocket : PublisherSocketBase
{
    public XPubSocketOptions XPubOptions { get; }

    public XPubSocket(Context context)
        : base(context, SocketType.XPub)
    {
        XPubOptions = new XPubSocketOptions(this);
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
