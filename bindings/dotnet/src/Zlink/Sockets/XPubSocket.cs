// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class XPubSocket : PublisherSocketBase
{
    public XPubSocket(Context context)
        : base(context, SocketType.XPub)
    {
    }

    public void SetVerbose(bool enabled)
    {
        SetOption(SocketOptions.XPubVerbose, enabled ? 1 : 0);
    }

    public void SetVerboser(bool enabled)
    {
        SetOption(SocketOptions.XPubVerboser, enabled ? 1 : 0);
    }

    public void SetNoDrop(bool enabled)
    {
        SetOption(SocketOptions.XPubNoDrop, enabled ? 1 : 0);
    }

    public SubscriptionEvent ReceiveSubscriptionEvent()
    {
        return Kernel.ReceiveSubscriptionEvent();
    }

    public SubscriptionEvent? TryReceiveSubscriptionEvent()
    {
        return Kernel.TryReceiveSubscriptionEvent();
    }
}
