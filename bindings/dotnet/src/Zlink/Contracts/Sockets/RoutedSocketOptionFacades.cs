// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Typed facade over DEALER-specific socket options.
/// </summary>
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

    /// <summary>
    /// Sets whether to send an empty probe message on connect so the peer
    /// immediately learns this socket's routing id.
    /// </summary>
    public bool Probe
    {
        set => Socket.SetOption(SocketOptions.ProbeRouter, value ? 1 : 0);
    }

    /// <summary>
    /// Sets how long a request issued by this DEALER waits for a reply before
    /// reporting a timeout; null waits indefinitely.
    /// </summary>
    public TimeSpan? RequestTimeout
    {
        set => Socket.SetOption(SocketOptions.DealerRequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Sets this peer's relative load-balancing weight (0-100); higher values
    /// receive a larger share of round-robined sends.
    /// </summary>
    public int PeerWeight
    {
        set => Socket.SetOption(SocketOptions.DealerWeight,
            CommonSocketOptions.EncodePeerWeight(value, nameof(value)));
    }
}

/// <summary>
/// Typed facade over ROUTER-specific socket options.
/// </summary>
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

    /// <summary>
    /// Gets or sets whether sending to an unknown route raises an error
    /// instead of silently dropping the message.
    /// </summary>
    public bool Mandatory
    {
        get => Socket.GetOption(SocketOptions.RouterMandatory) != 0;
        set => Socket.SetOption(SocketOptions.RouterMandatory, value ? 1 : 0);
    }

    /// <summary>
    /// Gets or sets whether a new peer reusing an existing routing id takes it
    /// over (true) or is rejected (false). Shorthand over
    /// <see cref="CommonSocketOptions.RoutingIdDuplicatePolicy"/>.
    /// </summary>
    public bool Handover
    {
        get => RoutingIdDuplicatePolicy == RidDuplicatePolicy.Handover;
        set => RoutingIdDuplicatePolicy = value
            ? RidDuplicatePolicy.Handover
            : RidDuplicatePolicy.Reject;
    }

    /// <summary>
    /// Gets or sets whether to send an empty probe message on connect so the
    /// peer immediately learns this socket's routing id.
    /// </summary>
    public bool Probe
    {
        get => Socket.GetOption(SocketOptions.ProbeRouter) != 0;
        set => Socket.SetOption(SocketOptions.ProbeRouter, value ? 1 : 0);
    }

    /// <summary>
    /// Gets the routing id assigned to the next outgoing connection, or null
    /// when none is pending. Set it with <see cref="SetConnectRoutingId"/>.
    /// </summary>
    public RoutingId? ConnectRoutingId
    {
        get
        {
            byte[] bytes = Socket.GetOption(SocketOptions.ConnectRoutingId);
            return bytes.Length == 0 ? null : RoutingId.From(bytes);
        }
    }

    /// <summary>
    /// Assigns <paramref name="routingId"/> to the next connection this socket
    /// initiates, instead of letting the peer choose one.
    /// </summary>
    public void SetConnectRoutingId(RoutingId routingId)
    {
        Socket.SetOption(SocketOptions.ConnectRoutingId, routingId.ToBytes());
    }

    /// <summary>
    /// Gets or sets how long a request issued by this ROUTER waits for a reply
    /// before reporting a timeout; null waits indefinitely.
    /// </summary>
    public TimeSpan? RequestTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            Socket.GetOption(SocketOptions.RouterRequestTimeout));
        set => Socket.SetOption(SocketOptions.RouterRequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    /// <summary>
    /// Gets or sets this peer's relative load-balancing weight (0-100); higher
    /// values receive a larger share of round-robined sends.
    /// </summary>
    public int PeerWeight
    {
        get => Socket.GetOption(SocketOptions.RouterWeight);
        set => Socket.SetOption(SocketOptions.RouterWeight,
            CommonSocketOptions.EncodePeerWeight(value, nameof(value)));
    }
}
