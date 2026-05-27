// SPDX-License-Identifier: MPL-2.0

using System.Collections.Generic;

namespace Systems.Zlink;

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
