// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class XPubSocket : PublisherSocketBase
{
    public XPubSocket(Context context)
        : base(context, SocketType.XPub)
    {
    }

    public void ReceiveSubscriptionEvent(out string topic, out bool subscribed,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.ReceiveSubscriptionEvent(out topic, out subscribed, flags);
    }

    public void ReceiveSubscriptionEvent(out string routingId, out string topic,
        out bool subscribed, ReceiveFlags flags = ReceiveFlags.None)
    {
        Kernel.ReceiveSubscriptionEvent(out routingId, out topic, out subscribed,
            flags);
    }
}
