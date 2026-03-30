// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class DealerSocket : MessageSocketBase
{
    public DealerSocket(Context context)
        : base(context, SocketType.Dealer)
    {
    }

    public void SetRoutingId(string routingId)
    {
        SetOption(SocketOptions.RoutingId, routingId);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        SetOption(SocketOptions.RoutingId, routingId.Value);
    }

    public string GetRoutingId()
    {
        return GetOption(SocketOptions.RoutingId);
    }

    public RoutingId GetRoutingIdValue()
    {
        return new RoutingId(GetRoutingId());
    }
}
