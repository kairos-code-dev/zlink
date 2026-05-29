// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Represents the spot send ready handler callback.
/// </summary>
public delegate void SpotSendReadyHandler();

/// <summary>
/// Represents the spot dispatch handler callback.
/// </summary>
public delegate void SpotDispatchHandler(SpotDispatchInfo info);

/// <summary>
/// Defines the spot contract.
/// </summary>
public interface ISpot : IZlinkSocket, IDisposable, IAsyncDisposable
{
    /// <summary>
    /// Gets the routing id.
    /// </summary>
    RoutingId RoutingId { get; }
    /// <summary>
    /// Starts a request operation.
    /// </summary>
    TimeSpan? RequestTimeout { get; set; }
    /// <summary>
    /// Starts a send operation.
    /// </summary>
    int SendHighWaterMark { get; set; }
    /// <summary>
    /// Receives the next available item.
    /// </summary>
    int ReceiveHighWaterMark { get; set; }
    /// <summary>
    /// Starts a send operation.
    /// </summary>
    int SendBufferSize { get; set; }
    /// <summary>
    /// Receives the next available item.
    /// </summary>
    int ReceiveBufferSize { get; set; }
    /// <summary>
    /// Starts a send operation.
    /// </summary>
    TimeSpan? SendTimeout { get; set; }
    /// <summary>
    /// Receives the next available item.
    /// </summary>
    TimeSpan? ReceiveTimeout { get; set; }
    /// <summary>
    /// Gets or sets the linger.
    /// </summary>
    TimeSpan? Linger { get; set; }

    /// <summary>
    /// Sets the routing id.
    /// </summary>
    void SetRoutingId(RoutingId routingId);
    /// <summary>
    /// Starts a publish operation.
    /// </summary>
    SendOperation Publish(string topic);
    /// <summary>
    /// Starts a send operation.
    /// </summary>
    SendOperation SendToChannel(string channelName);
    /// <summary>
    /// Starts a request operation.
    /// </summary>
    RequestOperation RequestToChannel(string channelName);
    /// <summary>
    /// Starts a send operation.
    /// </summary>
    SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    /// <summary>
    /// Starts a request operation.
    /// </summary>
    RequestOperation RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    /// <summary>
    /// Starts a request operation.
    /// </summary>
    RequestOperation RequestToRouter(RoutingId peerRid);
    /// <summary>
    /// Starts a reply operation.
    /// </summary>
    ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq);
    /// <summary>
    /// Starts a reply operation.
    /// </summary>
    ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq);
    /// <summary>
    /// Sets the subscription.
    /// </summary>
    void SetSubscription(string topicOrPattern);
    /// <summary>
    /// Unsets the subscription.
    /// </summary>
    void UnsetSubscription(string topicOrPattern);
    /// <summary>
    /// Gets the subscription at the specified index.
    /// </summary>
    SubscriptionEntry? SubscriptionAt(int index);
    /// <summary>
    /// Receives a subscribed topic message.
    /// </summary>
    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
    /// <summary>
    /// Receives the next available item.
    /// </summary>
    bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None);

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    bool RecvRouted(Received result, RecvFlags flags = RecvFlags.None);
    /// <summary>
    /// Receives the next available item.
    /// </summary>
    ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None);
    /// <summary>
    /// Receives the next available item.
    /// </summary>
    SpotActorLifecycleEvent? RecvActorLifecycle(
        RecvFlags flags = RecvFlags.None);
    /// <summary>
    /// Starts a reply operation.
    /// </summary>
    ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request,
        int joinResultCode);
    /// <summary>
    /// Gets actor entries.
    /// </summary>
    ActorRef[] Actors();

    /// <summary>
    /// Sets the send ready handler.
    /// </summary>
    void SetSendReadyHandler(SpotSendReadyHandler handler);
    /// <summary>
    /// Sets the dispatch handler.
    /// </summary>
    void SetDispatchHandler(SpotDispatchHandler handler);
    /// <summary>
    /// Closes the resource.
    /// </summary>
    void Close();
}
