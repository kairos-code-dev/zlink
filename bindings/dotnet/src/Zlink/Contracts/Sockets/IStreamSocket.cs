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
    /// Gets or sets the options.
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
    /// Gets or sets the detach stream.
    /// </summary>
    void DetachStream();

    /// <summary>
    /// Disconnects from the endpoint.
    /// </summary>
    void DisconnectRid(RoutingId peerRid);

    /// <summary>
    /// Gets or sets the attach actor gateway.
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
    /// Gets or sets the bound actors.
    /// </summary>
    IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid);
}
