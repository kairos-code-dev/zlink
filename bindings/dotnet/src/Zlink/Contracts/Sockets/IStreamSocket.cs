// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     STREAM callback for framed packet dispatch.
///     Message ownership is transferred to the callback.
///     The callback must dispose each message exactly once.
/// </summary>
public delegate void StreamPacketHandler(RoutingId routingId, Message header,
    Message body);

/// <summary>
///     Contract for a STREAM socket: exchanges framed packets with raw TCP peers.
/// </summary>
public interface IStreamSocket : IRoutedMessageSocket
{
    /// <summary>
    ///     Gets the STREAM-specific typed options facade.
    /// </summary>
    new StreamSocketOptions Options { get; }

    /// <summary>
    ///     Sets the routing id that identifies this socket to its peers. Apply
    ///     before connecting so peers observe it from the first packet.
    /// </summary>
    void SetRoutingId(RoutingId routingId);

    /// <summary>
    ///     Gets the routing id that identifies this socket to its peers.
    /// </summary>
    RoutingId GetRoutingId();

    /// <summary>
    ///     Registers the handler invoked for each inbound framed packet. The
    ///     handler runs on a background dispatch thread and owns its messages (see
    ///     <see cref="StreamPacketHandler" />).
    /// </summary>
    void OnPacket(StreamPacketHandler handler);

    /// <summary>
    ///     Disconnects the peer identified by <paramref name="peerRid" />.
    /// </summary>
    void DisconnectRid(RoutingId peerRid);
}