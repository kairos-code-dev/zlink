// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IRoutedMessageSocket : ISocket
{
    SendOperation Send(RoutingId routingId);

    bool Recv(Received result, RecvFlags flags = RecvFlags.None);

    void OnSendReady(Action handler);
}

public interface IConnectableRoutedMessageSocket : IRoutedMessageSocket,
    IConnectableSocket
{
}

public interface IRouterSocket : IConnectableRoutedMessageSocket
{
    new RouterSocketOptions Options { get; }

    void AttachDiscovery(IDiscovery discovery);

    void SetRoutingId(RoutingId routingId);

    RoutingId GetRoutingId();

    RequestOperation Request(RoutingId peerRid);

    ReplyOperation Reply(RoutingId rid, ulong requestSeq);

    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);

    RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid);

    ReplyOperation ReplyToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid, ulong requestSeq);
}
