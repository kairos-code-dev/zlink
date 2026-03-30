// SPDX-License-Identifier: MPL-2.0

namespace Zlink;

public sealed class RouterSocket : RoutedMessageSocketBase
{
    public RouterSocket(Context context)
        : base(context, SocketType.Router)
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

    public void SetMandatory(bool enabled)
    {
        SetOption(SocketOptions.RouterMandatory, enabled ? 1 : 0);
    }

    public bool GetMandatory()
    {
        return GetOption(SocketOptions.RouterMandatory) != 0;
    }
}
