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

public interface IStreamSocket : IRoutedMessageSocket
{
    new StreamSocketOptions Options { get; }

    void SetRoutingId(RoutingId routingId);

    RoutingId GetRoutingId();

    void OnPacket(StreamPacketHandler handler);

    void DetachStream();

    void DisconnectRid(RoutingId peerRid);

    void AttachActorGateway(ISpotNode node);

    ActorBindOperation BindActor(RoutingId sessionRid, ActorRef actor);

    ActorUnbindOperation UnbindActor(RoutingId sessionRid, string actorId);

    SendOperation SendBoundActor(RoutingId sessionRid, string actorId);

    IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid);
}
