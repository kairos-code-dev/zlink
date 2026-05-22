// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

public interface ISocket : IZlinkSocket, IDisposable, IAsyncDisposable
{
    CommonSocketOptions Options { get; }

    void Bind(string address);

    void Unbind(string address);

    ISocketMonitor MonitorOpen(SocketEvent events = SocketEvent.All);

    void SetChannelName(string channelName);

    void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false);

    void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false);

    void Close();
}

public interface IConnectableSocket : ISocket
{
    void Connect(string address);

    void Disconnect(string address);

    void DisconnectRid(RoutingId peerRid);
}

public interface IMessageSocket : IConnectableSocket
{
    SendOperation Send();

    bool Send(Message message, SendFlags flags = SendFlags.None);

    bool RecvPart(Message result, out bool hasMore,
        RecvFlags flags = RecvFlags.None);

    bool Recv(Received result, RecvFlags flags = RecvFlags.None);

    void OnSendReady(Action handler);
}

public interface IRoutedMessageSocket : ISocket
{
    SendOperation Send(RoutingId routingId);

    bool Send(RoutingId routingId, Message message,
        SendFlags flags = SendFlags.None);

    bool RecvPart(Message result, out RoutingId? routingId,
        out bool hasMore, RecvFlags flags = RecvFlags.None);

    bool Recv(Received result, RecvFlags flags = RecvFlags.None);

    void OnSendReady(Action handler);
}

public interface IConnectableRoutedMessageSocket : IRoutedMessageSocket,
    IConnectableSocket
{
}

public interface IPublisherSocket : IConnectableSocket
{
    SendOperation Publish(string topic);

    void OnSendReady(Action handler);
}

public interface ISubscriberSocket : IConnectableSocket
{
    void SetSubscription(string topicOrPattern);

    void UnsetSubscription(string topicOrPattern);

    SubscriptionEntry? SubscriptionAt(int index);

    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
}

public interface IDealerSocket : IMessageSocket
{
    new DealerSocketOptions Options { get; }

    void SetRoutingId(RoutingId routingId);

    RoutingId GetRoutingId();

    void AttachDiscovery(IDiscovery discovery);

    new void SetChannelName(string channelName);

    string GetChannelName();

    RequestOperation Request();
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

public interface IStreamSocket : IRoutedMessageSocket
{
    new StreamSocketOptions Options { get; }

    void SetRoutingId(RoutingId routingId);

    RoutingId GetRoutingId();

    void OnPacket(StreamPacketHandler handler);

    void DetachStream();

    void DisconnectRid(RoutingId peerRid);

    void AttachActorGateway(SpotNode node);

    ActorBindOperation BindActor(RoutingId sessionRid, ActorRef actor);

    ActorUnbindOperation UnbindActor(RoutingId sessionRid, string actorId);

    SendOperation SendBoundActor(RoutingId sessionRid, string actorId);

    IReadOnlyList<ActorRef> BoundActors(RoutingId sessionRid);
}

public interface IPubSocket : IPublisherSocket
{
    new PubSocketOptions Options { get; }

    void AttachDiscovery(IDiscovery discovery);
}

public interface ISubSocket : ISubscriberSocket
{
    new SubSocketOptions Options { get; }

    void AttachDiscovery(IDiscovery discovery);
}

public interface IXPubSocket : IPublisherSocket
{
    new PubSocketOptions Options { get; }

    bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None);
}

public interface IXSubSocket : ISubscriberSocket
{
    new SubSocketOptions Options { get; }
}
