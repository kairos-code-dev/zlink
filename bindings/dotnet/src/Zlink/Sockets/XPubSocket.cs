// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class XPubSocket : PublisherSocketBase
{
    public XPubSocketOptions XPubOptions { get; }

    public XPubSocket(Context context)
        : base(context, SocketType.XPub)
    {
        XPubOptions = new XPubSocketOptions(this);
    }

    public SubscriptionEvent ReceiveSubscriptionEvent()
    {
        return Kernel.ReceiveSubscriptionEvent();
    }

    public bool TryReceiveSubscriptionEvent(out SubscriptionEvent? subscriptionEvent)
    {
        subscriptionEvent = Kernel.TryReceiveSubscriptionEvent();
        return subscriptionEvent != null;
    }
}
