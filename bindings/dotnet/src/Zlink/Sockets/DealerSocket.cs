// SPDX-License-Identifier: MPL-2.0

using Zlink.Service;

namespace Zlink;

public sealed class DealerSocket : MessageSocketBase
{
    public DealerSocketOptions DealerOptions { get; }

    public DealerSocket(Context context)
        : base(context, SocketType.Dealer)
    {
        DealerOptions = new DealerSocketOptions(this);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        Kernel.SetOption(SocketOptions.RoutingId, routingId.Value);
    }

    public RoutingId GetRoutingId()
    {
        return new RoutingId(Kernel.GetOption(SocketOptions.RoutingId));
    }

    public void AttachDiscovery(Discovery discovery)
    {
        Kernel.AttachDiscovery(discovery);
    }
}
