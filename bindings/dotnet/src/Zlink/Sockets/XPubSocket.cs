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

    public SubscriptionEvent ReceiveSubscriptionEvent(
        RecvFlags flags = RecvFlags.None)
    {
        return Kernel.ReceiveSubscriptionEvent(flags);
    }

    internal bool TryReceiveSubscriptionEvent(out SubscriptionEvent? subscriptionEvent)
    {
        subscriptionEvent = Kernel.TryReceiveSubscriptionEvent();
        return subscriptionEvent != null;
    }
}
