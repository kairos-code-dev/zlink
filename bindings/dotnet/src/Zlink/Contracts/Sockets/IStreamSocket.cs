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
    ///     Detaches the stream peer currently attached to this socket.
    /// </summary>
    void DetachStream();

    /// <summary>
    ///     Disconnects the peer identified by <paramref name="peerRid" />.
    /// </summary>
    void DisconnectRid(RoutingId peerRid);

    /// <summary>
    ///     Binds <paramref name="actor" /> to the session
    ///     <paramref name="sessionRid" />; submit the returned operation to apply
    ///     the binding.
    /// </summary>
    ActorBindOperation BindActor(RoutingId sessionRid, ActorRef actor);

    /// <summary>
    ///     Removes the binding of <paramref name="actorId" /> from the session
    ///     <paramref name="sessionRid" />; submit the returned operation to apply it.
    /// </summary>
    ActorUnbindOperation UnbindActor(RoutingId sessionRid, string actorId);

    /// <summary>
    ///     Begins a send addressed to the bound actor <paramref name="actorId" /> on
    ///     session <paramref name="sessionRid" />. The parts are consumed on a
    ///     successful submit (see <see cref="SendOperation" /> for the ownership
    ///     contract).
    /// </summary>
    SendOperation SendBoundActor(RoutingId sessionRid, string actorId);

    /// <summary>
    ///     Lists the actors currently bound to the session
    ///     <paramref name="sessionRid" />.
    /// </summary>
    IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid);
}