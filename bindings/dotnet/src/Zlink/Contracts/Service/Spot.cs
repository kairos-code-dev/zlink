// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public delegate void SpotSendReadyHandler();

public delegate void SpotDispatchHandler(SpotDispatchInfo info);

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
    SendOperation SendToChannel(string channelName);
    RequestOperation RequestToChannel(string channelName);
    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOperation RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOperation RequestToRouter(RoutingId peerRid);
    ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq);
    ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq);
    void SetSubscription(string topicOrPattern);
    void UnsetSubscription(string topicOrPattern);
    SubscriptionEntry? SubscriptionAt(int index);
    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
    bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None);

    bool RecvRouted(Received result, RecvFlags flags = RecvFlags.None);
    ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None);
    SpotActorLifecycleEvent? RecvActorLifecycle(
        RecvFlags flags = RecvFlags.None);
    ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request,
        int joinResultCode);
    ActorRef[] Actors();

    void SetSendReadyHandler(SpotSendReadyHandler handler);
    void SetDispatchHandler(SpotDispatchHandler handler);
    void Close();
}
