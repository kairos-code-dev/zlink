// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;

namespace Systems.Zlink;

/// <summary>
/// STREAM callback for framed packet dispatch.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once.
/// </summary>
public delegate void StreamPacketHandler(RoutingId routingId, Message header,
    Message body);

/// <summary>
/// Defines the stream socket contract.
/// </summary>
public interface IStreamSocket : IRoutedMessageSocket
{
    /// <summary>
    /// Gets the options.
    /// </summary>
    new StreamSocketOptions Options { get; }

    /// <summary>
    /// Sets the routing id.
    /// </summary>
    void SetRoutingId(RoutingId routingId);

    /// <summary>
    /// Gets the routing id.
    /// </summary>
    RoutingId GetRoutingId();

    /// <summary>
    /// Registers a handler for packet.
    /// </summary>
    void OnPacket(StreamPacketHandler handler);

    /// <summary>
    /// Detaches a stream peer.
    /// </summary>
    void DetachStream();

    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectRid(RoutingId peerRid);

    /// <summary>
    /// Attaches an actor gateway to the stream socket.
    /// </summary>
    void AttachActorGateway(ISpotNode node);

    /// <summary>
    /// Binds the endpoint.
    /// </summary>
    ActorBindOperation BindActor(RoutingId sessionRid, ActorRef actor);

    /// <summary>
    /// Unbinds the endpoint.
    /// </summary>
    ActorUnbindOperation UnbindActor(RoutingId sessionRid, string actorId);

    /// <summary>
    /// Starts a send operation.
    /// </summary>
    SendOperation SendBoundActor(RoutingId sessionRid, string actorId);

    /// <summary>
    /// Gets actor bindings for the stream socket.
    /// </summary>
    IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid);
}
