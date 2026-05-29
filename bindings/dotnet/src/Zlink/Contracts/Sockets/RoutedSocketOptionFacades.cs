// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public sealed class DealerSocketOptions : CommonSocketOptions
{
    internal DealerSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    internal RoutingId RoutingId
    {
        get => RoutingId.From(Socket.GetOption(SocketOptions.RoutingId));
        set => Socket.SetOption(SocketOptions.RoutingId, value.ToBytes());
    }

    public bool Probe
    {
        set => Socket.SetOption(SocketOptions.ProbeRouter, value ? 1 : 0);
    }

    public TimeSpan? RequestTimeout
    {
        set => Socket.SetOption(SocketOptions.DealerRequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public int PeerWeight
    {
        set => Socket.SetOption(SocketOptions.DealerWeight,
            CommonSocketOptions.EncodePeerWeight(value, nameof(value)));
    }
}

public sealed class RouterSocketOptions : CommonSocketOptions
{
    internal RouterSocketOptions(ISocketOptionEndpoint socket)
        : base(socket)
    {
    }

    internal RoutingId RoutingId
    {
        get => RoutingId.From(Socket.GetOption(SocketOptions.RoutingId));
        set => Socket.SetOption(SocketOptions.RoutingId, value.ToBytes());
    }

    public bool Mandatory
    {
        get => Socket.GetOption(SocketOptions.RouterMandatory) != 0;
        set => Socket.SetOption(SocketOptions.RouterMandatory, value ? 1 : 0);
    }

    public bool Handover
    {
        get => RoutingIdDuplicatePolicy == RidDuplicatePolicy.Handover;
        set => RoutingIdDuplicatePolicy = value
            ? RidDuplicatePolicy.Handover
            : RidDuplicatePolicy.Reject;
    }

    public bool Probe
    {
        get => Socket.GetOption(SocketOptions.ProbeRouter) != 0;
        set => Socket.SetOption(SocketOptions.ProbeRouter, value ? 1 : 0);
    }

    public RoutingId? ConnectRoutingId
    {
        get
        {
            byte[] bytes = Socket.GetOption(SocketOptions.ConnectRoutingId);
            return bytes.Length == 0 ? null : RoutingId.From(bytes);
        }
    }

    public void SetConnectRoutingId(RoutingId routingId)
    {
        Socket.SetOption(SocketOptions.ConnectRoutingId, routingId.ToBytes());
    }

    public TimeSpan? RequestTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            Socket.GetOption(SocketOptions.RouterRequestTimeout));
        set => Socket.SetOption(SocketOptions.RouterRequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public int PeerWeight
    {
        get => Socket.GetOption(SocketOptions.RouterWeight);
        set => Socket.SetOption(SocketOptions.RouterWeight,
            CommonSocketOptions.EncodePeerWeight(value, nameof(value)));
    }
}
