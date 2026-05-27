// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface ISpot : IZlinkSocket, IDisposable, IAsyncDisposable
{
    RoutingId RoutingId { get; }
    TimeSpan? RequestTimeout { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    int SendBufferSize { get; set; }
    int ReceiveBufferSize { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? Linger { get; set; }

    void SetRoutingId(RoutingId routingId);
    SendOperation Publish(string topic);
    bool Publish(string topic, Message message, SendFlags flags = SendFlags.None);
    SendOperation SendChannel(string channelName);
    RequestOperation RequestChannel(string channelName);
    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None);
    RequestOperation RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOperation RequestToRouter(RoutingId peerRid);
    ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid, ulong requestSeq);
    ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq);
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    SubscriptionEntry? SubscriptionAt(int index);
    bool SubscribePart(Message result, Span<byte> topicBuffer,
        out int topicLength, out bool hasMore,
        RecvFlags flags = RecvFlags.None);
    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
    bool ReceiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags = RecvFlags.None);
    void OnSendReady(Action handler);
    bool RecvRoutedPart(Message result, out RoutingId? routingId,
        out RoutingId? spotRid, out ulong? requestSeq, out bool hasMore,
        RecvFlags flags = RecvFlags.None);
    bool RecvRouted(Received result, RecvFlags flags = RecvFlags.None);
    ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None);
    ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request, int joinResultCode);
    ActorRef[] ActorsSnapshot();
    void OnRoutedReceive(Action<Received> handler);
    void OnDispatchEvent(Action<SpotDispatchInfo> handler);
    void OnActorLifecycle(Action<SpotActorLifecycleInfo>? onJoin, Action<SpotActorLifecycleInfo>? onLeave);
    void Close();
}
